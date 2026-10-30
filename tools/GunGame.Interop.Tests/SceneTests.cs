using System.Text.Json;
using GunGame.Interop;
using GunGame.Scene;

namespace GunGame.Interop.Tests;

public class SceneDocumentTests
{
    [Fact]
    public void AddMoveRemove_MaintainTreeAndRaiseStructureChanged()
    {
        var doc = new SceneDocument();
        int events = 0;
        doc.StructureChanged += () => events++;

        var a = new SceneNode { Name = "A" };
        var b = new SceneNode { Name = "B" };
        var c = new SceneNode { Name = "C" };
        doc.Add(a);
        doc.Add(b, a);
        doc.Add(c, b);
        Assert.Equal(["A", "B", "C"], doc.Nodes().Select(n => n.Name));
        Assert.Same(b, c.Parent);

        doc.Move(c, null);
        Assert.Null(c.Parent);
        Assert.Contains(c, doc.Roots);
        Assert.DoesNotContain(c, b.Children);

        doc.Remove(a);  // 자손 B도 함께 사라진다
        Assert.Equal(["C"], doc.Nodes().Select(n => n.Name));
        Assert.Equal(5, events);
    }

    [Fact]
    public void Move_UnderSelfOrDescendant_IsRejectedAndTreeUnchanged()
    {
        var doc = new SceneDocument();
        var a = new SceneNode { Name = "A" };
        var b = new SceneNode { Name = "B" };
        doc.Add(a);
        doc.Add(b, a);

        Assert.Throws<InvalidOperationException>(() => doc.Move(a, b));
        Assert.Throws<InvalidOperationException>(() => doc.Move(a, a));
        Assert.Equal(["A", "B"], doc.Nodes().Select(n => n.Name));
        Assert.Same(a, b.Parent);
    }

    [Fact]
    public void Add_NodeAlreadyInScene_Throws()
    {
        var doc = new SceneDocument();
        var a = new SceneNode();
        doc.Add(a);
        Assert.Throws<InvalidOperationException>(() => doc.Add(a));
        Assert.Throws<InvalidOperationException>(() => doc.Remove(new SceneNode()));
    }
}

public class SceneSerializerTests
{
    private static SceneDocument SampleDocument(TestAssets assets)
    {
        var doc = new SceneDocument();
        var root = new SceneNode
        {
            Name = "Root",
            MeshPath = assets.WriteCubeObj(),
            TexturePath = assets.WriteQuadTextureBmp(),
            Position = new Vec3(1, 2, 3),
            Rotation = new Vec3(0.1f, 0.2f, 0.3f),
            Scale = new Vec3(2, 1, 0.5f),
            Color = new Vec3(0.9f, 0.6f, 0.3f),
        };
        doc.Add(root);
        doc.Add(new SceneNode { Name = "Group", Position = new Vec3(0, 1, 0) }, root);
        doc.Add(new SceneNode { Name = "Leaf", MeshPath = assets.Path_("cube.obj") }, root.Children[0]);
        return doc;
    }

    [Fact]
    public void SaveLoad_RoundTripsAllFields()
    {
        using var assets = new TestAssets();
        SceneDocument original = SampleDocument(assets);
        string scenePath = assets.Path_("level.scene.json");
        SceneSerializer.Save(original, scenePath);

        SceneDocument loaded = SceneSerializer.Load(scenePath);
        var a = original.Nodes().ToList();
        var b = loaded.Nodes().ToList();
        Assert.Equal(a.Count, b.Count);
        for (int i = 0; i < a.Count; i++)
        {
            Assert.Equal(a[i].Name, b[i].Name);
            Assert.Equal(a[i].MeshPath, b[i].MeshPath);
            Assert.Equal(a[i].TexturePath, b[i].TexturePath);
            Assert.Equal(a[i].Position, b[i].Position);
            Assert.Equal(a[i].Rotation, b[i].Rotation);
            Assert.Equal(a[i].Scale, b[i].Scale);
            Assert.Equal(a[i].Color, b[i].Color);
            Assert.Equal(a[i].Parent?.Name, b[i].Parent?.Name);
        }
    }

    [Fact]
    public void Save_WritesRelativeForwardSlashPaths()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        string sub = Path.Combine(assets.Directory, "models");
        Directory.CreateDirectory(sub);
        File.WriteAllText(Path.Combine(sub, "m.obj"), TestAssets.CubeObj);
        doc.Add(new SceneNode { MeshPath = Path.Combine(sub, "m.obj") });
        string scenePath = assets.Path_("s.json");
        SceneSerializer.Save(doc, scenePath);

        using JsonDocument json = JsonDocument.Parse(File.ReadAllText(scenePath));
        Assert.Equal(1, json.RootElement.GetProperty("version").GetInt32());
        Assert.Equal("models/m.obj", json.RootElement.GetProperty("nodes")[0].GetProperty("mesh").GetString());
    }

    [Fact]
    public void Load_MovedSceneFolder_ResolvesPathsRelativeToNewLocation()
    {
        using var assets = new TestAssets();
        string dirA = Path.Combine(assets.Directory, "a"), dirB = Path.Combine(assets.Directory, "b");
        Directory.CreateDirectory(dirA);
        Directory.CreateDirectory(dirB);
        File.WriteAllText(Path.Combine(dirA, "m.obj"), TestAssets.CubeObj);
        File.WriteAllText(Path.Combine(dirB, "m.obj"), TestAssets.CubeObj);
        var doc = new SceneDocument();
        doc.Add(new SceneNode { MeshPath = Path.Combine(dirA, "m.obj") });
        SceneSerializer.Save(doc, Path.Combine(dirA, "s.json"));
        File.Move(Path.Combine(dirA, "s.json"), Path.Combine(dirB, "s.json"));

        SceneDocument loaded = SceneSerializer.Load(Path.Combine(dirB, "s.json"));
        Assert.Equal(Path.Combine(dirB, "m.obj"), loaded.Roots[0].MeshPath);
    }

    [Theory]
    [InlineData("{ not json")]
    [InlineData("{\"version\": 99, \"nodes\": []}")]
    [InlineData("{\"version\": 1}")]
    [InlineData("{\"version\": 1, \"nodes\": [{\"position\": [1, 2]}]}")]
    [InlineData("{\"version\": 1, \"nodes\": [{\"scale\": [1, 2, 3, 4]}]}")]
    public void Load_InvalidFile_ThrowsSceneFormatException(string content)
    {
        using var assets = new TestAssets();
        string path = assets.Path_("bad.json");
        File.WriteAllText(path, content);
        Assert.Throws<SceneFormatException>(() => SceneSerializer.Load(path));
    }

    [Fact]
    public void Load_ExcessiveDepth_Throws()
    {
        using var assets = new TestAssets();
        string nested = "{}";
        for (int i = 0; i < 100; i++)
            nested = "{\"children\":[" + nested + "]}";
        string path = assets.Path_("deep.json");
        File.WriteAllText(path, "{\"version\":1,\"nodes\":[" + nested + "]}");
        Assert.Throws<SceneFormatException>(() => SceneSerializer.Load(path));
    }
}

public class SceneSyncTests
{
    private static byte[] RenderScene(SceneDocument doc, Action<SceneSync>? afterAttach = null)
    {
        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);
        afterAttach?.Invoke(sync);
        r.SetCamera(new Vec3(6, 5, 9), new Vec3(0, 0, 0), 0.8f);
        r.Render();
        return r.Pixels.ToArray();
    }

    private static SceneDocument CubeScene(TestAssets assets, out SceneNode root, out SceneNode child)
    {
        var doc = new SceneDocument();
        root = new SceneNode { Name = "Root", MeshPath = assets.WriteCubeObj(), Color = new Vec3(0.9f, 0.6f, 0.3f) };
        child = new SceneNode
        {
            Name = "Child",
            MeshPath = assets.Path_("cube.obj"),
            TexturePath = assets.WriteQuadTextureBmp(),
            Position = new Vec3(2.5f, 0, 0),
            Scale = new Vec3(0.5f, 0.5f, 0.5f),
        };
        doc.Add(root);
        doc.Add(child, root);
        return doc;
    }

    [Fact]
    public void SaveLoad_ProducesIdenticalRender()
    {
        using var assets = new TestAssets();
        SceneDocument doc = CubeScene(assets, out _, out _);
        byte[] before = RenderScene(doc);

        string path = assets.Path_("scene.json");
        SceneSerializer.Save(doc, path);
        byte[] after = RenderScene(SceneSerializer.Load(path));

        Assert.Equal(before, after);
        Assert.NotEqual(RenderScene(new SceneDocument()), before);  // 빈 씬과는 다르다
    }

    [Fact]
    public void ChangingNodeProperty_UpdatesEngineAndRaisesChanged()
    {
        using var assets = new TestAssets();
        SceneDocument doc = CubeScene(assets, out SceneNode root, out _);
        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);
        int changed = 0;
        sync.Changed += () => changed++;
        r.SetCamera(new Vec3(6, 5, 9), new Vec3(0, 0, 0), 0.8f);

        r.Render();
        byte[] before = r.Pixels.ToArray();
        root.Position = new Vec3(1, 0, 0);
        r.Render();
        byte[] moved = r.Pixels.ToArray();
        Assert.NotEqual(before, moved);
        Assert.Equal(1, changed);

        root.Name = "Renamed";  // 이름 변경은 렌더에 영향이 없고 알림도 없다
        Assert.Equal(1, changed);
        r.Render();
        Assert.Equal(moved, r.Pixels.ToArray());
    }

    [Fact]
    public void StructureChanges_AreReflected()
    {
        using var assets = new TestAssets();
        SceneDocument doc = CubeScene(assets, out SceneNode root, out SceneNode child);
        byte[] withChild = RenderScene(doc);

        // 자식을 루트로 옮기면 부모 변환(없음)이 사라지는 것과 같지만, 부모를 움직이면 결과가 달라져야 한다.
        root.Position = new Vec3(0, 1, 0);
        byte[] parentMoved = RenderScene(doc);
        Assert.NotEqual(withChild, parentMoved);
        doc.Move(child, null);
        byte[] childDetached = RenderScene(doc);
        Assert.NotEqual(parentMoved, childDetached);

        doc.Remove(root);
        doc.Remove(child);
        Assert.Equal(RenderScene(new SceneDocument()), RenderScene(doc));
    }

    [Fact]
    public void MissingMesh_ReportsErrorWithoutThrowing_AndRecoversWhenFixed()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        var node = new SceneNode { MeshPath = assets.Path_("not_there.obj") };
        doc.Add(node);

        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);
        Assert.Single(sync.Errors);
        r.SetCamera(new Vec3(6, 5, 9), new Vec3(0, 0, 0), 0.8f);
        r.Render();
        byte[] empty = r.Pixels.ToArray();

        node.MeshPath = assets.WriteCubeObj("fixed.obj");
        Assert.Empty(sync.Errors);
        r.Render();
        Assert.NotEqual(empty, r.Pixels.ToArray());
    }

    [Fact]
    public void GroupNode_TransformsChildrenButDrawsNothing()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        var group = new SceneNode { Name = "Group", Position = new Vec3(1, 0, 0) };
        var cube = new SceneNode { MeshPath = assets.WriteCubeObj() };
        doc.Add(group);
        doc.Add(cube, group);

        var flat = new SceneDocument();
        flat.Add(new SceneNode { MeshPath = assets.Path_("cube.obj"), Position = new Vec3(1, 0, 0) });
        Assert.Equal(RenderScene(flat), RenderScene(doc));
    }
}

public class SceneSyncReloadTests
{
    private const string BigQuadObj = "v -3 -3 0\nv 3 -3 0\nv 3 3 0\nv -3 3 0\nf 1 2 3 4\n";

    // 같은 시각의 덮어쓰기를 변경으로 인식하지 못하는 일이 없도록 수정 시각을 확실히 앞으로 민다.
    private static void Touch(string path) => File.SetLastWriteTimeUtc(path, File.GetLastWriteTimeUtc(path).AddSeconds(10));

    private static SceneDocument OneNode(string meshPath, string? texturePath = null)
    {
        var doc = new SceneDocument();
        doc.Add(new SceneNode { Name = "n", MeshPath = meshPath, TexturePath = texturePath, Color = new Vec3(0.9f, 0.6f, 0.3f) });
        return doc;
    }

    private static byte[] Frame(EngineRenderer r)
    {
        r.SetCamera(new Vec3(6, 5, 9), new Vec3(0, 0, 0), 0.8f);
        r.Render();
        return r.Pixels.ToArray();
    }

    private static byte[] FreshFrame(string meshPath, string? texturePath = null)
    {
        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(OneNode(meshPath, texturePath));
        return Frame(r);
    }

    [Fact]
    public void EditedMeshFile_IsReloadedOnRebuild()
    {
        using var assets = new TestAssets();
        string path = assets.WriteCubeObj("model.obj");
        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(OneNode(path));
        byte[] before = Frame(r);

        sync.Rebuild();  // 파일이 그대로면 같은 화면
        Assert.Equal(before, Frame(r));

        File.WriteAllText(path, BigQuadObj);  // 에디터 밖에서 파일을 다시 내보낸 상황
        Touch(path);
        sync.Rebuild();
        byte[] after = Frame(r);
        Assert.NotEqual(before, after);
        Assert.Equal(FreshFrame(path), after);  // 새로 시작한 에디터와 같은 화면
        Assert.Empty(sync.Errors);
    }

    [Fact]
    public void EditedTextureFile_IsReloadedOnRebuild()
    {
        using var assets = new TestAssets();
        string mesh = assets.WriteCubeObj();
        string texture = assets.WriteQuadTextureBmp();
        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(OneNode(mesh, texture));
        byte[] before = Frame(r);

        byte[] bytes = File.ReadAllBytes(texture);
        for (int i = 54; i < bytes.Length; i++)  // 픽셀 영역의 색을 반전
            bytes[i] = (byte)(255 - bytes[i]);
        File.WriteAllBytes(texture, bytes);
        Touch(texture);
        sync.Rebuild();
        byte[] after = Frame(r);
        Assert.NotEqual(before, after);
        Assert.Equal(FreshFrame(mesh, texture), after);
        Assert.Empty(sync.Errors);
    }

    [Fact]
    public void ReloadWhileAnimationPlays_DropsStaleClipsAndKeepsWorking()
    {
        using var assets = new TestAssets();
        string rig = new GltfBuilder().Write(assets.Directory, "rig");
        var doc = new SceneDocument();
        var node = new SceneNode { Name = "rig", MeshPath = rig, AnimationClip = "bend", AnimationPlaying = true };
        doc.Add(node);
        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);
        sync.Tick(0.3f);

        var changed = new GltfBuilder();
        changed.Animations.Clear();
        changed.Animations.Add(("bend", 0f, MathF.PI));  // 같은 이름의 클립이지만 다른 내용
        changed.Write(assets.Directory, "rig");
        Touch(rig);
        sync.Rebuild();  // 해제된 이전 클립 ID가 재생 상태에 남아 있으면 여기나 다음 Tick에서 엔진 오류가 난다
        sync.Tick(0.3f);
        r.Render();

        Assert.Empty(sync.Errors);
        Assert.Equal(["bend"], sync.GetClipNames(node));
    }
}
