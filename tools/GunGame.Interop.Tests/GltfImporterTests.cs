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
    public void Import_NonIdentityArmature_WarnsButSucceeds()
    {
        using var assets = new TestAssets();
        ImportedModel model = Import(new GltfBuilder { ArmatureTranslation = [0, 5, 0] }, assets);
        Assert.Contains(model.Warnings, w => w.Contains("armature"));
        Assert.NotNull(model.Skeleton);
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
