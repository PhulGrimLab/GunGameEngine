using System.Text;
using GunGame.Interop;

namespace GunGame.Scene;

/// <summary>씬을 구울 수 없을 때(메시/텍스처 파일 문제, 없는 클립/플레이어 노드, 구운 결과가 엔진에서 로드되지 않음 등).</summary>
public sealed class BakeException(string message, Exception? inner = null) : Exception(message, inner);

/// <summary>
/// 씬 문서를 독립 실행 플레이어가 읽는 바이너리 씬(.ggs)으로 굽는다. 씬 JSON이 참조하는 메시(.obj/.ggm/.glb/.gltf)는 엔진용
/// 배열(정점/인덱스, 스킨/스켈레톤/클립)로, 텍스처는 이미지 파일 원본 바이트로 파일에 합친다. 같은 경로의 메시/텍스처는 한 번만 넣는다.
/// 결과는 입력이 같으면 바이트까지 같다(재현 가능한 빌드). 형식은 engine/src/scene_io.cpp 맨 위 주석이 기준이다.
/// 파일에 쓰기 전에 실제 엔진으로 다시 로드해 보고(verify) 로드되지 않으면 쓰지 않는다: 스폰이 방 밖이거나 영역 모양이 잘못된
/// 경우처럼 구울 때 알 수 없는 문제를 플레이어에서 처음 만나지 않도록 한다.
/// </summary>
public static class SceneBaker
{
    /// <summary>쓰는 형식 버전. 정책: 쓰기는 항상 최신, 엔진 로더는 1..Version을 읽는다(버전 1 = 충돌체에 이름 없음).</summary>
    public const uint Version = 2;

    private const int FloatsPerVertex = 8;
    private const int GgmHeaderBytes = 12;
    private const uint ChecksumPolynomial = 0xEDB88320u;

    /// <summary>씬을 구워 path에 쓴다(임시 파일에 먼저 쓰고 교체). verify가 true면 쓰기 전에 엔진으로 로드해 본다.</summary>
    /// <exception cref="BakeException">구울 수 없거나 검증에 실패했을 때(사유 포함). 이 경우 기존 파일은 그대로다.</exception>
    public static void Bake(SceneDocument document, string path, bool verify = true)
    {
        byte[] bytes = BakeToBytes(document);
        string temp = $"{path}.{Guid.NewGuid():N}.tmp";
        try
        {
            File.WriteAllBytes(temp, bytes);
            if (verify)
                Verify(temp);
            if (File.Exists(path))
                File.Replace(temp, path, null);
            else
                File.Move(temp, path);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            throw new BakeException($"cannot write {path}: {ex.Message}", ex);
        }
        finally
        {
            try { File.Delete(temp); } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { /* 지우지 못해도 원래 결과를 우선한다 */ }
        }
    }

    /// <summary>엔진(WARP)에 실제로 올려 본다. 오류는 BakeException으로 바꾼다.</summary>
    private static void Verify(string path)
    {
        try
        {
            using var renderer = new EngineRenderer(64, 48, useWarp: true);
            using EngineScene scene = EngineScene.Load(renderer, path);
            scene.Step(0, 0, 0);  // 플레이어가 있는 씬의 첫 프레임(스폰/트리거/카메라)까지 확인한다
        }
        catch (EngineException ex)
        {
            throw new BakeException("the baked scene does not load in the engine: " + ex.Message, ex);
        }
    }

    /// <summary>씬을 .ggs 바이트로 만든다(파일 쓰기/엔진 검증 없음).</summary>
    public static byte[] BakeToBytes(SceneDocument document)
    {
        List<SceneNode> nodes = document.Nodes().ToList();  // 깊이 우선, 부모가 항상 앞에 온다
        var nodeIndex = new Dictionary<SceneNode, int>();
        for (int i = 0; i < nodes.Count; i++)
            nodeIndex[nodes[i]] = i;

        var meshes = new List<BakedMesh>();
        var meshIndexByPath = new Dictionary<string, int>();
        var textures = new List<byte[]>();
        var textureIndexByPath = new Dictionary<string, int>();
        var meshOfNode = new int[nodes.Count];
        var textureOfNode = new int[nodes.Count];
        for (int i = 0; i < nodes.Count; i++)
        {
            SceneNode node = nodes[i];
            meshOfNode[i] = string.IsNullOrEmpty(node.MeshPath) ? -1 : IndexOf(node.MeshPath, meshIndexByPath, meshes, path => LoadMesh(node, path));
            textureOfNode[i] = string.IsNullOrEmpty(node.TexturePath) ? -1 : IndexOf(node.TexturePath, textureIndexByPath, textures, path => LoadTexture(node, path));
            if (meshOfNode[i] >= 0 && !string.IsNullOrEmpty(node.AnimationClip) && meshes[meshOfNode[i]].Skeleton is not null &&
                meshes[meshOfNode[i]].Clips.All(c => c.Name != node.AnimationClip))
            {
                throw new BakeException($"node '{node.Name}': clip '{node.AnimationClip}' was not found in {node.MeshPath}");
            }
        }

        using var ms = new MemoryStream();
        using (var w = new BinaryWriter(ms, Encoding.UTF8, leaveOpen: true))
        {
            w.Write("GGS1"u8);
            w.Write(Version);

            w.Write((uint)textures.Count);
            foreach (byte[] image in textures)
            {
                w.Write((uint)image.Length);
                w.Write(image);
            }

            w.Write((uint)meshes.Count);
            foreach (BakedMesh mesh in meshes)
                WriteMesh(w, mesh);

            w.Write((uint)nodes.Count);
            for (int i = 0; i < nodes.Count; i++)
                WriteNode(w, nodes[i], nodes[i].Parent is null ? -1 : nodeIndex[nodes[i].Parent!], meshOfNode[i], textureOfNode[i]);

            WriteLighting(w, document.Lighting);
            WriteGameplay(w, document.Gameplay, nodes, nodeIndex);
        }

        byte[] body = ms.ToArray();
        byte[] result = new byte[body.Length + sizeof(uint)];
        body.CopyTo(result, 0);
        BitConverter.GetBytes(Crc32(body)).CopyTo(result, body.Length);
        return result;
    }

    // ---- 리소스 읽기 ----
    private sealed record BakedMesh(float[] Vertices, uint[] Indices, uint[]? Joints, float[]? Weights, SkeletonData? Skeleton, IReadOnlyList<ClipData> Clips);

    private static int IndexOf<T>(string path, Dictionary<string, int> indexByPath, List<T> list, Func<string, T> load)
    {
        if (indexByPath.TryGetValue(path, out int index))
            return index;
        list.Add(load(path));
        return indexByPath[path] = list.Count - 1;
    }

    private static BakedMesh LoadMesh(SceneNode node, string path)
    {
        try
        {
            switch (Path.GetExtension(path).ToLowerInvariant())
            {
                case ".glb":
                case ".gltf":
                    ImportedModel model = GltfImporter.Import(path);
                    return new BakedMesh(model.Vertices, model.Indices, model.Joints, model.Weights, model.Skeleton, model.Clips);
                case ".ggm":
                    return ReadGgm(path);
                default:
                    // OBJ는 엔진이 읽는다: .ggm으로 변환한 뒤 그 바이트를 쓴다(OBJ 파서를 C#에 따로 두지 않는다).
                    string temp = Path.Combine(Path.GetTempPath(), $"gge_bake_{Guid.NewGuid():N}.ggm");
                    try
                    {
                        EngineRenderer.ConvertObjToGgm(path, temp);
                        return ReadGgm(temp);
                    }
                    finally
                    {
                        try { File.Delete(temp); } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { /* 임시 파일 */ }
                    }
            }
        }
        catch (Exception ex) when (ex is EngineException or GltfException or IOException or UnauthorizedAccessException or NotSupportedException or InvalidDataException)
        {
            throw new BakeException($"node '{node.Name}': cannot read mesh {path}: {ex.Message}", ex);
        }
    }

    private static byte[] LoadTexture(SceneNode node, string path)
    {
        try
        {
            byte[] bytes = File.ReadAllBytes(path);
            if (bytes.Length == 0)
                throw new InvalidDataException("the file is empty");
            return bytes;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or NotSupportedException or InvalidDataException)
        {
            throw new BakeException($"node '{node.Name}': cannot read texture {path}: {ex.Message}", ex);
        }
    }

    /// <summary>.ggm: "GGM1", 정점 수, 인덱스 수, 정점 float[수*8], 인덱스 uint[수].</summary>
    private static BakedMesh ReadGgm(string path)
    {
        byte[] file = File.ReadAllBytes(path);
        if (file.Length < GgmHeaderBytes || file[0] != 'G' || file[1] != 'G' || file[2] != 'M' || file[3] != '1')
            throw new InvalidDataException("not a GGM1 mesh file");
        uint vertexCount = BitConverter.ToUInt32(file, 4), indexCount = BitConverter.ToUInt32(file, 8);
        long expected = GgmHeaderBytes + (long)vertexCount * FloatsPerVertex * sizeof(float) + (long)indexCount * sizeof(uint);
        if (vertexCount == 0 || indexCount == 0 || file.Length != expected)
            throw new InvalidDataException("GGM1 file size does not match its header");
        var vertices = new float[vertexCount * FloatsPerVertex];
        var indices = new uint[indexCount];
        Buffer.BlockCopy(file, GgmHeaderBytes, vertices, 0, vertices.Length * sizeof(float));
        Buffer.BlockCopy(file, GgmHeaderBytes + vertices.Length * sizeof(float), indices, 0, indices.Length * sizeof(uint));
        return new BakedMesh(vertices, indices, null, null, null, []);
    }

    // ---- 쓰기 ----
    private static void WriteString(BinaryWriter w, string? text)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(text ?? "");
        w.Write((uint)bytes.Length);
        w.Write(bytes);
    }

    private static void WriteVec3(BinaryWriter w, Vec3 v)
    {
        w.Write(v.X);
        w.Write(v.Y);
        w.Write(v.Z);
    }

    private static void WriteFloats(BinaryWriter w, float[] values)
    {
        foreach (float v in values)
            w.Write(v);
    }

    private static void WriteMesh(BinaryWriter w, BakedMesh mesh)
    {
        bool skinned = mesh.Joints is not null && mesh.Weights is not null && mesh.Skeleton is not null;
        w.Write((byte)(skinned ? 1 : 0));
        w.Write((uint)(mesh.Vertices.Length / FloatsPerVertex));
        w.Write((uint)mesh.Indices.Length);
        WriteFloats(w, mesh.Vertices);
        foreach (uint index in mesh.Indices)
            w.Write(index);
        if (!skinned)
            return;

        foreach (uint joint in mesh.Joints!)
            w.Write(joint);
        WriteFloats(w, mesh.Weights!);
        SkeletonData skeleton = mesh.Skeleton!;
        w.Write((uint)skeleton.BoneCount);
        foreach (int parent in skeleton.Parents)
            w.Write(parent);
        WriteFloats(w, skeleton.InverseBind);
        WriteFloats(w, skeleton.RestTranslation);
        WriteFloats(w, skeleton.RestRotation);
        WriteFloats(w, skeleton.RestScale);
        w.Write((uint)mesh.Clips.Count);
        foreach (ClipData clip in mesh.Clips)
        {
            WriteString(w, clip.Name);
            w.Write(clip.Duration);
            w.Write((uint)clip.Channels.Count);
            foreach (ClipChannelData channel in clip.Channels)
            {
                w.Write(channel.Bone);
                w.Write((uint)channel.Path);
                w.Write((uint)channel.Times.Length);
                WriteFloats(w, channel.Times);
                WriteFloats(w, channel.Values);
            }
        }
    }

    private static void WriteNode(BinaryWriter w, SceneNode node, int parent, int mesh, int texture)
    {
        WriteString(w, node.Name);
        w.Write(parent);
        w.Write(mesh);
        w.Write(texture);
        WriteVec3(w, node.Position);
        WriteVec3(w, node.Rotation);
        WriteVec3(w, node.Scale);
        WriteVec3(w, node.Color);
        w.Write(node.Alpha);
        w.Write((byte)(node.CastsShadow ? 1 : 0));
        WriteString(w, node.AnimationClip);
        w.Write((byte)(node.AnimationPlaying ? 1 : 0));
        w.Write(node.AnimationSpeed);
    }

    private static void WriteLighting(BinaryWriter w, LightingData lighting)
    {
        WriteVec3(w, lighting.Ambient);
        WriteVec3(w, lighting.SunDirection);
        WriteVec3(w, lighting.SunColor);
        w.Write((uint)lighting.PointLights.Count);
        foreach (PointLightDef light in lighting.PointLights)
        {
            WriteVec3(w, light.Position);
            WriteVec3(w, light.Color);
            w.Write(light.Range);
        }
        w.Write((byte)(lighting.ShadowEnabled ? 1 : 0));
        w.Write(lighting.ShadowPlaneY);
        w.Write(lighting.ShadowOpacity);
    }

    private static void WriteGameplay(BinaryWriter w, GameplayData gameplay, List<SceneNode> nodes, Dictionary<SceneNode, int> nodeIndex)
    {
        w.Write((uint)gameplay.Rooms.Count);
        foreach (RoomDef room in gameplay.Rooms)
        {
            WriteString(w, room.Name);
            w.Write((uint)(room.Polygon.Length / 2));
            WriteFloats(w, room.Polygon);
            WriteVec3(w, room.Eye);
            WriteVec3(w, room.Target);
            w.Write(room.Fov);
        }

        w.Write((uint)gameplay.Colliders.Count);
        foreach (ColliderDef c in gameplay.Colliders)
        {
            WriteString(w, c.Name);   // version 2부터: 이름(스크립트가 이름으로 켜고 끈다). 같은 이름 여럿 가능
            w.Write(c.MinX);
            w.Write(c.MinZ);
            w.Write(c.MaxX);
            w.Write(c.MaxZ);
        }

        w.Write((uint)gameplay.Triggers.Count);
        foreach (TriggerDef t in gameplay.Triggers)
        {
            WriteString(w, t.Name);
            w.Write((uint)(t.Polygon.Length / 2));
            WriteFloats(w, t.Polygon);
            w.Write(t.Tag);
        }

        if (gameplay.Player is not { } player)
        {
            w.Write((byte)0);
            return;
        }
        // 에디터의 플레이 모드(PlaySession.Start)와 같은 검사와 같은 사유 문구를 쓴다.
        if (gameplay.Rooms.Count == 0)
            throw new BakeException("the scene has no gameplay.rooms (at least one room is needed for the camera)");
        SceneNode node = nodes.FirstOrDefault(n => n.Name == player.Node)
            ?? throw new BakeException($"player node '{player.Node}' was not found in the scene");
        if (node.Parent is not null)
            throw new BakeException($"player node '{player.Node}' must be a root node");
        w.Write((byte)1);
        w.Write(nodeIndex[node]);
        w.Write((byte)(player.Spawn is null ? 0 : 1));
        w.Write(player.Spawn?.X ?? 0f);
        w.Write(player.Spawn?.Z ?? 0f);
        w.Write(player.Radius);
        w.Write(player.Speed);
    }

    /// <summary>CRC-32(IEEE, 반사형 0xEDB88320): 엔진 로더의 검증과 같은 값이다.</summary>
    public static uint Crc32(ReadOnlySpan<byte> data)
    {
        uint crc = 0xFFFFFFFFu;
        foreach (byte b in data)
        {
            crc ^= b;
            for (int k = 0; k < 8; k++)
                crc = (crc & 1) != 0 ? (crc >> 1) ^ ChecksumPolynomial : crc >> 1;
        }
        return ~crc;
    }
}
