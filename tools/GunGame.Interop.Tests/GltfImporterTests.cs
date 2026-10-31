using System.Numerics;
using GunGame.Interop;

namespace GunGame.Interop.Tests;

public class GltfImporterTests
{
    private static ImportedModel Import(GltfBuilder builder, TestAssets assets, string name = "model") =>
        GltfImporter.Import(builder.Write(assets.Directory, name));

    [Fact]
    public void Import_Glb_ReordersJointsParentFirstAndRemapsVertexData()
    {
        using var assets = new TestAssets();
        ImportedModel model = Import(new GltfBuilder(), assets);  // skin.joints는 부모가 뒤인 역순

        SkeletonData skeleton = Assert.IsType<SkeletonData>(model.Skeleton);
        Assert.Equal([-1, 0, 1], skeleton.Parents);
        Assert.Equal([0f, 0, 0, 0, 1, 0, 0, 1, 0], skeleton.RestTranslation);
        for (int bone = 0; bone < 3; bone++)  // 역바인드는 본 b에서 이동(0,-b,0), 새 순서로 재배열됨
            Assert.Equal(-bone, skeleton.InverseBind[bone * 16 + 13]);

        Assert.Equal(48, model.Vertices.Length);
        Assert.Equal(12, model.Indices.Length);
        // 행0 → 본0, 행1 → 본1, 행2 → 본1/본2 반반 (가중치 0인 슬롯의 조인트는 0)
        Assert.Equal<uint>([0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0], model.Joints!);
        Assert.Equal([1f, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0.5f, 0.5f, 0, 0, 0.5f, 0.5f, 0, 0], model.Weights!);

        ClipData clip = Assert.Single(model.Clips);
        Assert.Equal("bend", clip.Name);
        Assert.Equal(1f, clip.Duration);
        ClipChannelData channel = Assert.Single(clip.Channels);
        Assert.Equal(1, channel.Bone);  // 본1 (원래 skin 조인트 번호 1 → 새 번호 1)
        Assert.Equal(ChannelPath.Rotation, channel.Path);
        Assert.Empty(model.Warnings);
    }

    [Fact]
    public void Import_UnreversedJointOrder_GivesSameResult()
    {
        using var assets = new TestAssets();
        ImportedModel reversed = Import(new GltfBuilder(), assets, "a");
        ImportedModel straight = Import(new GltfBuilder { ReverseJointOrder = false }, assets, "b");
        Assert.Equal(reversed.Skeleton!.Parents, straight.Skeleton!.Parents);
        Assert.Equal(reversed.Joints, straight.Joints);
        Assert.Equal(reversed.Skeleton.InverseBind, straight.Skeleton.InverseBind);
    }

    [Fact]
    public void Import_DataUriAndExternalBin_MatchGlb()
    {
        using var assets = new TestAssets();
        ImportedModel glb = Import(new GltfBuilder(), assets, "glb");
        ImportedModel dataUri = Import(new GltfBuilder { Glb = false }, assets, "uri");
        ImportedModel external = Import(new GltfBuilder { Glb = false, ExternalBin = true }, assets, "ext");

        foreach (ImportedModel other in new[] { dataUri, external })
        {
            Assert.Equal(glb.Vertices, other.Vertices);
            Assert.Equal(glb.Indices, other.Indices);
            Assert.Equal(glb.Joints, other.Joints);
            Assert.Equal(glb.Weights, other.Weights);
            Assert.Equal(glb.Skeleton!.InverseBind, other.Skeleton!.InverseBind);
            Assert.Equal(glb.Clips[0].Channels[0].Values, other.Clips[0].Channels[0].Values);
        }
    }

    [Fact]
    public void Import_ThenEngine_BonePositionsFollowTheClip()
    {
        // 임포트 → 엔진 리소스 생성 → 포즈 조회까지: 재정렬, 역바인드, 채널 본 번호가 모두 맞아야 한다.
        using var assets = new TestAssets();
        ImportedModel model = Import(new GltfBuilder(), assets);

        using var r = new EngineRenderer(64, 48, useWarp: true);
        int mesh = r.CreateSkinnedMesh(model.Vertices, model.Joints!, model.Weights!, model.Indices);
        int skeleton = r.CreateSkeleton(model.Skeleton!);
        int clip = r.CreateClip(model.Clips[0]);
        int obj = r.CreateObject(mesh);
        r.SetObjectSkeleton(obj, skeleton);

        float theta = MathF.PI / 4;  // 클립은 0 → 90°, t=0.5에서 45°
        r.SetObjectPose(obj, clip, 0.5f);
        float[] pose = r.GetObjectPose(obj, 3);
        Assert.Equal(-MathF.Sin(theta), pose[2 * 16 + 12], 4);
        Assert.Equal(1 + MathF.Cos(theta), pose[2 * 16 + 13], 4);
        Assert.Equal(1f, pose[1 * 16 + 13], 4);  // 본1은 회전의 영향을 받지 않는다
    }

    [Fact]
    public void Import_ThenEngine_SkinnedRenderChangesWithPose()
    {
        using var assets = new TestAssets();
        ImportedModel model = Import(new GltfBuilder(), assets);
        using var r = new EngineRenderer(96, 72, useWarp: true);
        r.CreateObject(r.CreateSkinnedMesh(model.Vertices, model.Joints!, model.Weights!, model.Indices));
        int obj = 1;
        r.SetObjectSkeleton(obj, r.CreateSkeleton(model.Skeleton!));
        int clip = r.CreateClip(model.Clips[0]);
        r.SetCamera(new Vec3(0, 1.2f, 6), new Vec3(0, 1.2f, 0), 0.8f);

        r.SetObjectPose(obj, clip, 0);
        r.Render();
        byte[] straight = r.Pixels.ToArray();
        r.SetObjectPose(obj, clip, 1);
        r.Render();
        Assert.NotEqual(straight, r.Pixels.ToArray());
    }

    [Theory]
    [InlineData("STEP")]
    [InlineData("CUBICSPLINE")]
    public void Import_UnsupportedInterpolation_Throws(string interpolation)
    {
        using var assets = new TestAssets();
        var ex = Assert.Throws<GltfException>(() => Import(new GltfBuilder { Interpolation = interpolation }, assets));
        Assert.Contains(interpolation, ex.Message);
    }

    [Fact]
    public void Import_NonIdentityArmature_BecomesASyntheticRootBone_AndMovesTheWholeModel()
    {
        using var assets = new TestAssets();
        ImportedModel plain = Import(new GltfBuilder(), assets, "plain");
        ImportedModel moved = Import(new GltfBuilder { ArmatureTranslation = [0, 5, 0] }, assets, "moved");

        Assert.Empty(moved.Warnings);  // 이제 무시하지 않고 반영한다
        SkeletonData skeleton = moved.Skeleton!;
        Assert.Equal([-1, 0, 1, 2], skeleton.Parents);                        // 합성 본(0)이 원래 루트 본(1)의 부모
        Assert.Equal([0f, 5, 0], skeleton.RestTranslation[..3]);
        Assert.Equal([0f, 0, 0, 1], skeleton.RestRotation[..4]);
        Assert.Equal([1f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], skeleton.InverseBind[..16]);
        Assert.Equal(plain.Skeleton!.InverseBind, skeleton.InverseBind[16..]);  // 실제 본의 역바인드는 그대로(한 칸 뒤)
        // 정점의 조인트와 클립 채널의 본 번호는 한 칸씩 밀린다(가중치 0인 슬롯은 0 그대로).
        Assert.Equal(plain.Joints!.Select((j, i) => plain.Weights![i] > 0 ? j + 1 : 0u), moved.Joints!);
        Assert.Equal(plain.Clips[0].Channels[0].Bone + 1, moved.Clips[0].Channels[0].Bone);

        // 엔진에서 실제로 모든 정점이 (0,5,0)만큼 올라가 있다: 레스트 포즈와 클립 중간 시점 모두.
        foreach ((int? clip, float time) in new (int?, float)[] { (null, 0f), (0, 0.5f) })
        {
            Vector3[] before = RealGltfModelTests.EnginePositions(plain, clip, time);
            Vector3[] after = RealGltfModelTests.EnginePositions(moved, clip, time);
            for (int v = 0; v < before.Length; v++)
                Assert.True(Vector3.Distance(before[v] + new Vector3(0, 5, 0), after[v]) < 1e-4f, $"vertex {v} at clip {clip}: {before[v]} -> {after[v]}");
        }
    }

    [Fact]
    public void Import_MissingNormals_GeneratesUnitNormals()
    {
        using var assets = new TestAssets();
        ImportedModel model = Import(new GltfBuilder { IncludeNormals = false }, assets);
        for (int v = 0; v < 6; v++)
        {
            float nx = model.Vertices[v * 8 + 3], ny = model.Vertices[v * 8 + 4], nz = model.Vertices[v * 8 + 5];
            Assert.Equal(1f, MathF.Sqrt(nx * nx + ny * ny + nz * nz), 4);
            Assert.Equal(1f, nz, 4);  // 띠는 +Z를 향한다
        }
    }

    [Fact]
    public void Import_StaticModel_AppliesNodeTransform()
    {
        using var assets = new TestAssets();
        ImportedModel model = Import(new GltfBuilder { Skinned = false, StaticNodeTranslation = [5, 0, 0] }, assets);
        Assert.Null(model.Skeleton);
        Assert.Null(model.Joints);
        Assert.Empty(model.Clips);
        Assert.Equal(4.7f, model.Vertices[0], 4);  // x = -0.3 + 5
        Assert.Equal(5.3f, model.Vertices[8], 4);  // x =  0.3 + 5
    }

    [Fact]
    public void Import_DuplicateAnimationNames_AreMadeUnique()
    {
        using var assets = new TestAssets();
        var builder = new GltfBuilder();
        builder.Animations.Clear();
        builder.Animations.AddRange([("walk", 0f, 1f), ("walk", 0f, 2f), ("idle", 0f, 0f)]);
        ImportedModel model = Import(builder, assets);
        Assert.Equal(["walk", "walk#1", "idle"], model.Clips.Select(c => c.Name));
    }

    [Fact]
    public void Import_TooManyJoints_Throws()
    {
        using var assets = new TestAssets();
        var ex = Assert.Throws<GltfException>(() => Import(new GltfBuilder { Bones = 65 }, assets));
        Assert.Contains("64", ex.Message);
    }

    [Fact]
    public void Import_InvalidFiles_ThrowGltfException()
    {
        using var assets = new TestAssets();

        string garbage = assets.Path_("garbage.glb");
        File.WriteAllBytes(garbage, [1, 2, 3, 4, 5, 6, 7, 8]);
        Assert.Throws<GltfException>(() => GltfImporter.Import(garbage));

        string badJson = assets.Path_("bad.gltf");
        File.WriteAllText(badJson, "{ not json");
        Assert.Throws<GltfException>(() => GltfImporter.Import(badJson));

        string noMeshes = assets.Path_("empty.gltf");
        File.WriteAllText(noMeshes, "{\"asset\":{\"version\":\"2.0\"}}");
        Assert.Throws<GltfException>(() => GltfImporter.Import(noMeshes));

        string good = new GltfBuilder().Write(assets.Directory, "good");
        byte[] bytes = File.ReadAllBytes(good);
        string truncated = assets.Path_("truncated.glb");
        File.WriteAllBytes(truncated, bytes[..(bytes.Length - 40)]);
        Assert.Throws<GltfException>(() => GltfImporter.Import(truncated));

        string wrongVersion = assets.Path_("v1.glb");
        byte[] v1 = (byte[])bytes.Clone();
        v1[4] = 1;
        File.WriteAllBytes(wrongVersion, v1);
        Assert.Throws<GltfException>(() => GltfImporter.Import(wrongVersion));

        Assert.Throws<GltfException>(() => Import(new GltfBuilder { TruncateBuffer = true }, assets, "shortbuf"));
        Assert.Throws<FileNotFoundException>(() => GltfImporter.Import(assets.Path_("missing.glb")));
    }

    // Task.Wait는 작업이 예외로 끝나면 그 예외를 다시 던지므로, 끝났는지만 기다리는 대기 핸들을 쓴다.
    private static bool Finishes(Task task) => ((IAsyncResult)task).AsyncWaitHandle.WaitOne(TimeSpan.FromSeconds(10));

    [Fact]
    public void Import_NodeCycleOutsideJoints_IsRejectedInsteadOfHanging()
    {
        // 악성 파일: Armature(0)와 메시 노드가 서로의 부모(각 노드의 부모는 정확히 하나)라 조상 탐색이 영원히 돈다.
        // 이전에는 ReadSkin의 부모 탐색에 순환 가드가 없어 UI 스레드가 멈췄다. 정지하면 시간 초과로 실패하게 한다.
        using var assets = new TestAssets();
        var builder = new GltfBuilder { Glb = false };
        string path = builder.Write(assets.Directory, "cycle");
        var doc = System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(path))!;
        var nodes = doc["nodes"]!.AsArray();
        int meshNode = nodes.Count - 1;
        nodes[0]!["children"] = new System.Text.Json.Nodes.JsonArray(1, meshNode);
        nodes[meshNode]!["children"] = new System.Text.Json.Nodes.JsonArray(0);
        File.WriteAllText(path, doc.ToJsonString());

        var import = Task.Run(() => GltfImporter.Import(path));
        Assert.True(Finishes(import), "importer did not terminate (infinite loop on a node cycle)");
        Assert.IsType<GltfException>(import.Exception!.InnerException);
    }

    [Fact]
    public void Import_SelfChildAndMultipleParents_AreRejected()
    {
        using var assets = new TestAssets();
        foreach ((string name, int[] children0) in new[] { ("self", new[] { 0, 1 }), ("multi", new[] { 1, 2 }) })
        {
            string path = new GltfBuilder { Glb = false }.Write(assets.Directory, name);
            var doc = System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(path))!;
            // self: 노드 0이 자기 자신을 자식으로. multi: 노드 2가 노드 0과 노드 1 양쪽의 자식이 된다.
            doc["nodes"]![0]!["children"] = new System.Text.Json.Nodes.JsonArray(children0.Select(c => (System.Text.Json.Nodes.JsonNode)c).ToArray());
            File.WriteAllText(path, doc.ToJsonString());
            var import = Task.Run(() => GltfImporter.Import(path));
            Assert.True(Finishes(import), $"{name}: importer did not terminate");
            Assert.IsType<GltfException>(import.Exception!.InnerException);
        }
    }
}

public class GltfImporterRobustnessTests
{
    // 빌더가 쓴 .gltf를 JSON으로 열어 일부를 악성/비정상 값으로 바꾼다.
    private static string WritePatched(TestAssets assets, GltfBuilder builder, string name, Action<System.Text.Json.Nodes.JsonNode> patch)
    {
        builder.Glb = false;
        string path = builder.Write(assets.Directory, name);
        var doc = System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(path))!;
        patch(doc);
        File.WriteAllText(path, doc.ToJsonString());
        return path;
    }

    [Theory]
    [InlineData(2_000_000_000)]  // count * 성분 수가 int를 넘는다(OverflowException)
    [InlineData(-1)]
    public void Accessor_WithoutBufferViewAndAbsurdCount_IsRejected(int count)
    {
        using var assets = new TestAssets();
        string path = WritePatched(assets, new GltfBuilder(), "hugecount", doc =>
        {
            var uv = doc["accessors"]!.AsArray().First(a => a!["type"]!.GetValue<string>() == "VEC2")!;
            uv.AsObject().Remove("bufferView");   // 버퍼 길이로 상한이 정해지지 않는 "0으로 채운" 접근자
            uv["count"] = count;
        });
        Assert.Throws<GltfException>(() => GltfImporter.Import(path));
    }

    [Fact]
    public void Accessor_WithoutBufferView_BeyondTheCap_IsRejectedBeforeAllocating()
    {
        // 위치/법선/UV 접근자를 모두 "bufferView 없음 + 1600만 개 초과"로 일관되게 바꿔 개수 불일치 검사를 통과시킨다.
        // 상한이 없으면 수 GB를 0으로 채워 만들기 시작하므로(OutOfMemory 위험) 파일을 열기 전에 거부해야 한다.
        // 주의: 상한 도입 전 코드로는 이 테스트를 돌리지 않았다(대용량 할당이 일어난다).
        using var assets = new TestAssets();
        string path = WritePatched(assets, new GltfBuilder(), "bigzero", doc =>
        {
            foreach (var accessor in doc["accessors"]!.AsArray())
            {
                string type = accessor!["type"]!.GetValue<string>();
                if (type is "VEC3" or "VEC2" && accessor["componentType"]!.GetValue<int>() == 5126)
                {
                    accessor.AsObject().Remove("bufferView");
                    accessor["count"] = (1 << 24) + 1;
                }
            }
        });
        var ex = Assert.Throws<GltfException>(() => GltfImporter.Import(path));
        Assert.Contains("count", ex.Message);
    }

    [Fact]
    public void Accessor_NegativeByteOffset_IsRejected()
    {
        // 접근자 오프셋이 음수면 자기 bufferView 앞쪽(다른 데이터)을 읽는다. glTF 규격은 0 이상만 허용한다.
        using var assets = new TestAssets();
        string path = WritePatched(assets, new GltfBuilder(), "negoffset", doc =>
        {
            var views = doc["bufferViews"]!.AsArray();
            var accessor = doc["accessors"]!.AsArray().First(a =>
                a!["bufferView"] is { } v && (views[v.GetValue<int>()]!["byteOffset"]?.GetValue<int>() ?? 0) >= 4)!;
            accessor["byteOffset"] = -4;
        });
        Assert.Throws<GltfException>(() => GltfImporter.Import(path));
    }

    [Theory]
    [InlineData(false)]  // 로컬 절대 경로
    [InlineData(true)]   // UNC(\host\share): 열면 SMB 연결과 NTLM 인증이 일어날 수 있다 — 파일을 열기 전에 거부해야 한다
    public void ExternalBufferUri_ThatIsAbsoluteOrNetwork_IsRejected(bool unc)
    {
        using var assets = new TestAssets();
        string path = WritePatched(assets, new GltfBuilder { ExternalBin = true }, "absuri", doc =>
        {
            string bin = Path.GetFullPath(Path.Combine(assets.Directory, doc["buffers"]![0]!["uri"]!.GetValue<string>()));
            doc["buffers"]![0]!["uri"] = unc ? "\\\\127.0.0.1\\nonexistent-share\\x.bin" : bin;
        });
        var ex = Assert.Throws<GltfException>(() => GltfImporter.Import(path));
        Assert.Contains("relative", ex.Message);
    }

    private static ImportedModel TubeWithMeshNodeScale(TestAssets assets, string name, float x, float y, float z)
    {
        string path = WritePatched(assets, new GltfBuilder { Skinned = false, Tube = true }, name, doc =>
        {
            var nodes = doc["nodes"]!.AsArray();
            nodes[nodes.Count - 1]!["scale"] = new System.Text.Json.Nodes.JsonArray(x, y, z);
        });
        return GltfImporter.Import(path);
    }

    [Fact]
    public void NonUniformScale_UsesInverseTransposeForNormals()
    {
        using var assets = new TestAssets();
        ImportedModel plain = TubeWithMeshNodeScale(assets, "plain", 1, 1, 1);
        ImportedModel scaled = TubeWithMeshNodeScale(assets, "scaled", 2, 1, 1);
        for (int v = 0; v < plain.Vertices.Length / 8; v++)
        {
            float nx = plain.Vertices[v * 8 + 3], nz = plain.Vertices[v * 8 + 5];
            // 위치는 x가 2배. 올바른 법선은 (nx/2, 0, nz)의 정규화(표면에 수직을 유지). 법선에 스케일을 그대로 곱하면 (2nx, 0, nz)가 된다.
            float len = MathF.Sqrt(nx * nx / 4 + nz * nz);
            Assert.Equal(2 * plain.Vertices[v * 8], scaled.Vertices[v * 8], 4);
            Assert.Equal(nx / 2 / len, scaled.Vertices[v * 8 + 3], 4);
            Assert.Equal(nz / len, scaled.Vertices[v * 8 + 5], 4);
        }
    }

    private static (int Total, int Outward) CountOutwardTriangles(ImportedModel m)
    {
        int total = 0, outward = 0;
        for (int t = 0; t < m.Indices.Length; t += 3)
        {
            (int a, int b, int c) = ((int)m.Indices[t], (int)m.Indices[t + 1], (int)m.Indices[t + 2]);
            var pa = new System.Numerics.Vector3(m.Vertices[a * 8], m.Vertices[a * 8 + 1], m.Vertices[a * 8 + 2]);
            var pb = new System.Numerics.Vector3(m.Vertices[b * 8], m.Vertices[b * 8 + 1], m.Vertices[b * 8 + 2]);
            var pc = new System.Numerics.Vector3(m.Vertices[c * 8], m.Vertices[c * 8 + 1], m.Vertices[c * 8 + 2]);
            var n = new System.Numerics.Vector3(m.Vertices[a * 8 + 3], m.Vertices[a * 8 + 4], m.Vertices[a * 8 + 5]);
            var geometric = System.Numerics.Vector3.Cross(pb - pa, pc - pa);
            total++;
            if (System.Numerics.Vector3.Dot(geometric, n) > 0)
                outward++;
        }
        return (total, outward);
    }

    [Fact]
    public void MirroredNode_KeepsTriangleWindingConsistentWithNormals()
    {
        // 음의 스케일(거울 반전)은 행렬식이 음수라 감김 방향이 뒤집힌다. 삼각형 순서를 바꿔 앞면(CCW)이 유지되어야 한다.
        using var assets = new TestAssets();
        (int total, int outward) = CountOutwardTriangles(TubeWithMeshNodeScale(assets, "unmirrored", 1, 1, 1));
        Assert.Equal(total, outward);  // 기준: 원본은 모든 삼각형이 법선 쪽을 향한다
        (total, outward) = CountOutwardTriangles(TubeWithMeshNodeScale(assets, "mirrored", -1, 1, 1));
        Assert.Equal(total, outward);
    }
}
