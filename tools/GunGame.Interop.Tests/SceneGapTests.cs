using System.Text;
using System.Text.Json.Nodes;
using GunGame.Interop;
using GunGame.Scene;

namespace GunGame.Interop.Tests;

/// <summary>C# 뮤테이션에서 살아남은 SceneSerializer/SceneSync 변이를 겨냥한 테스트(경계값과 반환값 확인).</summary>
public class SceneGapTests
{
    private static string WriteScene(TestAssets assets, string json, string name = "scene.json")
    {
        string path = assets.Path_(name);
        File.WriteAllText(path, json);
        return path;
    }

    private static string Reason(string path) => Assert.Throws<SceneFormatException>(() => SceneSerializer.Load(path)).Message;

    // ---- SceneSerializer ----
    [Fact]
    public void SavedFile_IsIndentedForHumans()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        doc.Add(new SceneNode { Name = "a" });
        string path = assets.Path_("saved.json");
        SceneSerializer.Save(doc, path);

        Assert.True(File.ReadAllLines(path).Length > 5, "scene files are meant to be diffed and edited by hand");
    }

    [Fact]
    public void AnimationSpeed_IsSavedOnlyWhenItMeansSomething()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        doc.Add(new SceneNode { Name = "defaultSpeed", AnimationClip = "walk", AnimationSpeed = 1f });  // 기본값이라 생략
        doc.Add(new SceneNode { Name = "fast", AnimationClip = "walk", AnimationSpeed = 2f });
        doc.Add(new SceneNode { Name = "noClip", AnimationSpeed = 3f });                                // 클립이 없으면 속도는 의미가 없다
        string path = assets.Path_("speed.json");
        SceneSerializer.Save(doc, path);

        JsonArray nodes = JsonNode.Parse(File.ReadAllText(path))!["nodes"]!.AsArray();
        Assert.Null(nodes[0]!["speed"]);
        Assert.Equal(2f, nodes[1]!["speed"]!.GetValue<float>());
        Assert.Null(nodes[2]!["speed"]);
    }

    private static string NestedScene(int levels)
    {
        var json = new StringBuilder("{\"version\":1,\"nodes\":[");
        for (int i = 0; i < levels; i++)
            json.Append("{\"name\":\"n").Append(i).Append("\",\"children\":[");
        for (int i = 0; i < levels; i++)  // 가장 안쪽 children 배열은 비어 있다
            json.Append("]}");
        return json.Append("]}").ToString();
    }

    [Fact]
    public void HierarchyDepth_Allows64Levels_AndRejects65WithItsOwnReason()
    {
        using var assets = new TestAssets();
        SceneDocument ok = SceneSerializer.Load(WriteScene(assets, NestedScene(64), "depth64.json"));
        int depth = 0;
        for (SceneNode? n = ok.Roots.Single(); n is not null; n = n.Children.SingleOrDefault())
            depth++;
        Assert.Equal(64, depth);

        Assert.Contains("too deep", Reason(WriteScene(assets, NestedScene(65), "depth65.json")));
    }

    private static string RoomScene(string polygonJson) =>
        "{\"version\":1,\"nodes\":[],\"gameplay\":{\"rooms\":[{\"name\":\"R\",\"polygon\":" + polygonJson +
        ",\"camera\":{\"eye\":[0,5,9],\"target\":[0,0,0]}}]}}";

    [Theory]
    [InlineData("[0,0, 4,0]")]                  // 꼭짓점 2개
    [InlineData("[0,0, 4,0, 4,4, 0]")]          // 숫자 7개: 길이는 충분하지만 홀수
    [InlineData("[0,0, 4,0, 4,4, 0,4, 1]")]     // 숫자 9개
    [InlineData("null")]
    public void RoomPolygon_NeedsThreeOrMoreWholePairs(string polygon)
    {
        using var assets = new TestAssets();
        Assert.Contains("'polygon' must be at least three", Reason(WriteScene(assets, RoomScene(polygon))));
    }

    [Fact]
    public void RoomPolygon_ExactlyThreePairs_IsAccepted()
    {
        using var assets = new TestAssets();
        SceneDocument doc = SceneSerializer.Load(WriteScene(assets, RoomScene("[0,0, 4,0, 0,4]")));
        Assert.Equal([0f, 0f, 4f, 0f, 0f, 4f], doc.Gameplay.Rooms.Single().Polygon);
    }

    [Fact]
    public void PlayerSpeed_ZeroIsAllowed_NegativeIsNot()
    {
        using var assets = new TestAssets();
        string Scene(string speed) => "{\"version\":1,\"nodes\":[],\"gameplay\":{\"player\":{\"node\":\"Hero\",\"speed\":" + speed + "}}}";

        Assert.Equal(0f, SceneSerializer.Load(WriteScene(assets, Scene("0"), "s0.json")).Gameplay.Player!.Speed);
        Assert.Contains("non-negative", Reason(WriteScene(assets, Scene("-0.5"), "s-.json")));
    }

    // ---- SceneSync ----
    private static byte[] Frame(EngineRenderer r)
    {
        r.SetCamera(new Vec3(6, 5, 9), new Vec3(0, 0, 0), 0.8f);
        r.Render();
        return r.Pixels.ToArray();
    }

    [Fact]
    public void Preview_ReportsWhetherTheNodeIsInTheScene()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        var inScene = new SceneNode { Name = "in", MeshPath = assets.WriteCubeObj() };
        doc.Add(inScene);
        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);

        Assert.True(sync.Preview(inScene, new Vec3(1, 0, 0), default));
        Assert.False(sync.Preview(new SceneNode { Name = "stranger" }, new Vec3(1, 0, 0), default));
    }

    [Fact]
    public void ChangingTheTexturePath_UpdatesTheObject_AndOtherChangesDoNot()
    {
        using var assets = new TestAssets();
        string texture = assets.WriteQuadTextureBmp();
        var doc = new SceneDocument();
        var node = new SceneNode { Name = "n", MeshPath = assets.WriteCubeObj(), TexturePath = texture, Color = new Vec3(1, 1, 1) };
        doc.Add(node);
        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);
        byte[] textured = Frame(r);

        node.TexturePath = null;           // 텍스처를 뗀다: 화면이 바뀌어야 한다
        byte[] plain = Frame(r);
        Assert.NotEqual(textured, plain);

        node.TexturePath = texture;        // 다시 붙이면 원래 화면
        Assert.Equal(textured, Frame(r));

        node.Position = new Vec3(0.5f, 0, 0);  // 위치만 바꿔도 텍스처가 유지된다(경로가 같으면 다시 읽지 않는다)
        byte[] moved = Frame(r);
        node.Position = default;
        Assert.Equal(textured, Frame(r));
        Assert.NotEqual(textured, moved);
        Assert.Empty(sync.Errors);
    }
}
