using System.Buffers.Binary;
using System.Text;
using System.Text.Json;

namespace GunGame.Interop.Tests;

/// <summary>
/// 테스트(와 샘플 에셋 생성)용 glTF 작성기: 본 N개 체인(본 i의 부모는 i-1, 로컬 이동 (0,1,0))에 3행 띠 메시를 스키닝한다.
/// 기본은 skin.joints를 부모-자식 역순(부모가 뒤)으로 나열해 임포터의 재정렬을 검증한다.
/// </summary>
public sealed class GltfBuilder
{
    public int Bones { get; set; } = 3;
    public bool Glb { get; set; } = true;
    public bool ExternalBin { get; set; }
    public bool ReverseJointOrder { get; set; } = true;
    public bool Skinned { get; set; } = true;
    public bool IncludeNormals { get; set; } = true;
    public string Interpolation { get; set; } = "LINEAR";
    public float[]? ArmatureTranslation { get; set; }
    public float[]? StaticNodeTranslation { get; set; }
    public List<(string Name, float Angle0, float Angle1)> Animations { get; } = [("bend", 0f, MathF.PI / 2)];
    public bool TruncateBuffer { get; set; }

    /// <summary>true면 띠 대신 높이 3의 부드러운 8각 튜브(본 y=0,1,2 사이를 선형 가중치로 섞음)를 만든다.</summary>
    public bool Tube { get; set; }

    /// <summary>여러 키프레임/본을 쓰는 애니메이션. Tracks의 각도는 Z축 회전(라디안)이고 Times와 길이가 같아야 한다.</summary>
    public List<(string Name, float[] Times, (int Bone, float[] Angles)[] Tracks)> KeyedAnimations { get; } = [];

    private readonly List<byte> _bin = [];
    private readonly List<object> _views = [];
    private readonly List<object> _accessors = [];

    private int AddAccessor(byte[] data, int componentType, int count, string type, bool normalized = false)
    {
        while (_bin.Count % 4 != 0)
            _bin.Add(0);
        _views.Add(new { buffer = 0, byteOffset = _bin.Count, byteLength = data.Length });
        _bin.AddRange(data);
        _accessors.Add(normalized
            ? new { bufferView = _views.Count - 1, componentType, count, type, normalized = true }
            : (object)new { bufferView = _views.Count - 1, componentType, count, type });
        return _accessors.Count - 1;
    }

    private static byte[] Floats(IEnumerable<float> values) => values.SelectMany(BitConverter.GetBytes).ToArray();

    /// <summary>파일 바이트를 만들고, ExternalBin이면 name.bin도 같은 폴더에 쓴다.</summary>
    public string Write(string directory, string name)
    {
        string path = Path.Combine(directory, name + (Glb ? ".glb" : ".gltf"));
        (string json, byte[] bin) = Build(name);
        if (Glb)
        {
            byte[] jsonBytes = Pad(Encoding.UTF8.GetBytes(json), 0x20);
            byte[] binBytes = Pad(bin, 0);
            using var ms = new MemoryStream();
            using var w = new BinaryWriter(ms);
            w.Write(0x46546C67u); w.Write(2u); w.Write((uint)(12 + 8 + jsonBytes.Length + 8 + binBytes.Length));
            w.Write((uint)jsonBytes.Length); w.Write(0x4E4F534Au); w.Write(jsonBytes);
            w.Write((uint)binBytes.Length); w.Write(0x004E4942u); w.Write(binBytes);
            File.WriteAllBytes(path, ms.ToArray());
        }
        else
        {
            File.WriteAllText(path, json);
            if (ExternalBin)
                File.WriteAllBytes(Path.Combine(directory, name + ".bin"), bin);
        }
        return path;
    }

    private static byte[] Pad(byte[] data, byte fill)
    {
        int padded = (data.Length + 3) & ~3;
        var result = new byte[padded];
        Array.Fill(result, fill);
        data.CopyTo(result, 0);
        return result;
    }

    private (string json, byte[] bin) Build(string name)
    {
        _bin.Clear(); _views.Clear(); _accessors.Clear();
        int n = Bones;

        // ---- 메시: 3행 x 2열 띠 (행0→본0, 행1→본1, 행2→본1/본2 반반) ----
        // 본 b의 skin 조인트 번호. 역순이면 부모가 뒤로 간다.
        int JointIndex(int bone) => ReverseJointOrder ? n - 1 - bone : bone;
        List<float> positionList = [], normalList = [], uvList = [], weightList = [];
        List<ushort> indexList = [];
        List<(int BoneA, int BoneB, float WeightB)> vertexBones = [];  // 정점별 (본A, 본B, B의 가중치)
        if (Tube)
        {
            const int rings = 13, sides = 8;
            for (int k = 0; k < rings; k++)
            {
                float y = k * 0.25f;
                int lower = (int)Math.Min(MathF.Floor(y), 1);
                float t = y >= 2 ? 1 : y - lower;
                (int a, int b) = y >= 2 ? (2, 2) : (lower, lower + 1);
                for (int j = 0; j < sides; j++)
                {
                    float angle = 2 * MathF.PI * j / sides;
                    float nx = MathF.Cos(angle), nz = MathF.Sin(angle);
                    positionList.AddRange([0.3f * nx, y, 0.3f * nz]);
                    normalList.AddRange([nx, 0, nz]);
                    uvList.AddRange([(float)j / sides, y / 3]);
                    vertexBones.Add((a, b, t));
                }
            }
            for (int k = 0; k + 1 < rings; k++)
                for (int j = 0; j < sides; j++)
                {
                    ushort a = (ushort)(k * sides + j), b = (ushort)(k * sides + (j + 1) % sides);
                    ushort d = (ushort)((k + 1) * sides + j), c = (ushort)((k + 1) * sides + (j + 1) % sides);
                    indexList.AddRange([a, d, c, a, c, b]);
                }
        }
        else
        {
            // 3행 x 2열 띠 (행0→본0, 행1→본1, 행2→본1/본2 반반)
            positionList.AddRange([-0.3f, 0, 0, 0.3f, 0, 0, -0.3f, 1, 0, 0.3f, 1, 0, -0.3f, 2, 0, 0.3f, 2, 0]);
            for (int v = 0; v < 6; v++)
                normalList.AddRange([0, 0, 1]);
            uvList.AddRange([0, 1, 1, 1, 0, 0.5f, 1, 0.5f, 0, 0, 1, 0]);
            indexList.AddRange([0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4]);
            int[] rowBone0 = [0, 1, 1], rowBone1 = [0, 0, Math.Min(2, n - 1)];
            float[] rowWeight1 = [0, 0, 0.5f];
            for (int v = 0; v < 6; v++)
                vertexBones.Add((rowBone0[v / 2], rowBone1[v / 2], rowWeight1[v / 2]));
        }
        int vertexCount = positionList.Count / 3;

        var attributes = new Dictionary<string, int>
        {
            ["POSITION"] = AddAccessor(Floats(positionList), 5126, vertexCount, "VEC3"),
        };
        if (IncludeNormals)
            attributes["NORMAL"] = AddAccessor(Floats(normalList), 5126, vertexCount, "VEC3");
        attributes["TEXCOORD_0"] = AddAccessor(Floats(uvList), 5126, vertexCount, "VEC2");
        if (Skinned)
        {
            var joints = new List<byte>();
            foreach ((int boneA, int boneB, float weightB) in vertexBones)
            {
                joints.AddRange([(byte)JointIndex(boneA), (byte)JointIndex(boneB), 0, 0]);
                weightList.AddRange([1 - weightB, weightB, 0, 0]);
            }
            attributes["JOINTS_0"] = AddAccessor(joints.ToArray(), 5121, vertexCount, "VEC4");
            attributes["WEIGHTS_0"] = AddAccessor(Floats(weightList), 5126, vertexCount, "VEC4");
        }
        int indexAccessor = AddAccessor(indexList.SelectMany(BitConverter.GetBytes).ToArray(), 5123, indexList.Count, "SCALAR");

        // ---- 노드: 0 = 아르마추어, 1..n = 본 체인, n+1 = 메시 ----
        var nodes = new List<object>();
        object armature = ArmatureTranslation is null
            ? new { name = "Armature", children = new[] { 1 } }
            : new { name = "Armature", translation = ArmatureTranslation, children = new[] { 1 } };
        nodes.Add(armature);
        for (int b = 0; b < n; b++)
        {
            float[] t = [0, b == 0 ? 0 : 1, 0];
            nodes.Add(b + 1 < n
                ? new { name = $"bone{b}", translation = t, children = new[] { b + 2 } }
                : (object)new { name = $"bone{b}", translation = t });
        }
        int meshNode = nodes.Count;
        if (Skinned)
            nodes.Add(new { name = "Mesh", mesh = 0, skin = 0 });
        else
            nodes.Add(StaticNodeTranslation is null
                ? new { name = "Mesh", mesh = 0 }
                : (object)new { name = "Mesh", mesh = 0, translation = StaticNodeTranslation });

        var doc = new Dictionary<string, object>
        {
            ["asset"] = new { version = "2.0" },
            ["scene"] = 0,
            ["scenes"] = new[] { new { nodes = new[] { 0, meshNode } } },
            ["nodes"] = nodes,
            ["meshes"] = new[] { new { primitives = new[] { new { attributes, indices = indexAccessor } } } },
        };

        if (Skinned)
        {
            // skin.joints[k] = 본 (ReverseJointOrder ? n-1-k : k)의 노드. 역바인드는 본 b에서 이동(0,-b,0).
            var jointNodes = new int[n];
            var ibm = new List<float>();
            for (int k = 0; k < n; k++)
            {
                int bone = ReverseJointOrder ? n - 1 - k : k;
                jointNodes[k] = bone + 1;
                float[] m = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -bone, 0, 1];
                ibm.AddRange(m);
            }
            int ibmAccessor = AddAccessor(Floats(ibm), 5126, n, "MAT4");
            doc["skins"] = new[] { new { joints = jointNodes, inverseBindMatrices = ibmAccessor, skeleton = 1 } };

            if (Animations.Count > 0 || KeyedAnimations.Count > 0)
            {
                var animations = new List<object>();
                foreach ((string animName, float[] times, (int Bone, float[] Angles)[] tracks) in KeyedAnimations)
                {
                    int input = AddAccessor(Floats(times), 5126, times.Length, "SCALAR");
                    var samplers = new List<object>();
                    var channels = new List<object>();
                    foreach ((int bone, float[] angles) in tracks)
                    {
                        float[] quats = angles.SelectMany(a => new[] { 0f, 0f, MathF.Sin(a / 2), MathF.Cos(a / 2) }).ToArray();
                        int output = AddAccessor(Floats(quats), 5126, angles.Length, "VEC4");
                        samplers.Add(new { input, output, interpolation = Interpolation });
                        channels.Add(new { sampler = samplers.Count - 1, target = new { node = bone + 1, path = "rotation" } });
                    }
                    animations.Add(new { name = animName, samplers, channels });
                }
                foreach ((string animName, float a0, float a1) in Animations)
                {
                    int input = AddAccessor(Floats([0f, 1f]), 5126, 2, "SCALAR");
                    int output = AddAccessor(Floats([0, 0, MathF.Sin(a0 / 2), MathF.Cos(a0 / 2), 0, 0, MathF.Sin(a1 / 2), MathF.Cos(a1 / 2)]), 5126, 2, "VEC4");
                    animations.Add(new
                    {
                        name = animName,
                        samplers = new[] { new { input, output, interpolation = Interpolation } },
                        channels = new[] { new { sampler = 0, target = new { node = 2, path = "rotation" } } },  // 본1
                    });
                }
                doc["animations"] = animations;
            }
        }

        doc["accessors"] = _accessors;
        doc["bufferViews"] = _views;

        byte[] bin = _bin.ToArray();
        if (TruncateBuffer)
            bin = bin[..(bin.Length - 16)];
        object buffer = Glb
            ? new { byteLength = _bin.Count }
            : ExternalBin
                ? new { byteLength = _bin.Count, uri = name + ".bin" }
                : (object)new { byteLength = _bin.Count, uri = "data:application/octet-stream;base64," + Convert.ToBase64String(_bin.ToArray()) };
        doc["buffers"] = new[] { buffer };
        return (JsonSerializer.Serialize(doc), bin);
    }
}
