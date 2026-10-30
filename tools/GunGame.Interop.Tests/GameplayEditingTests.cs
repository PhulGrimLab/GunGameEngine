using GunGame.Interop;
using GunGame.Scene;

namespace GunGame.Interop.Tests;

/// <summary>게임플레이 데이터 편집 로직(WPF 비의존). 편집 결과가 저장/로드/플레이 시작까지 이어지는지도 확인한다.</summary>
public class GameplayEditingTests
{
    [Fact]
    public void AddRoom_CentersDefaultRectangleOnCameraTarget_AndKeepsCamera()
    {
        var data = new GameplayData();
        RoomDef room = GameplayEditing.AddRoom(data, new Vec3(1, 5, 9), new Vec3(10, 0, 20), 0.7f);

        Assert.Same(room, Assert.Single(data.Rooms));
        Assert.Equal("Room", room.Name);
        Assert.Equal([6f, 17f, 14f, 17f, 14f, 23f, 6f, 23f], room.Polygon);  // 8 x 6, 중심 (10, 20)
        Assert.Equal(new Vec3(1, 5, 9), room.Eye);
        Assert.Equal(new Vec3(10, 0, 20), room.Target);
        Assert.Equal(0.7f, room.Fov);
    }

    [Fact]
    public void AddingSeveralItems_GivesUniqueNames()
    {
        var data = new GameplayData();
        GameplayEditing.AddRoom(data, new Vec3(0, 5, 9), default, 0.8f);
        GameplayEditing.AddRoom(data, new Vec3(0, 5, 9), default, 0.8f);
        GameplayEditing.AddCollider(data, 0, 0);
        GameplayEditing.AddCollider(data, 0, 0);
        data.Colliders[0].Name = "Collider 3";  // 다음 후보가 이미 쓰이면 건너뛴다
        GameplayEditing.AddCollider(data, 0, 0);

        Assert.Equal(["Room", "Room 2"], data.Rooms.Select(r => r.Name));
        Assert.Equal(["Collider 3", "Collider 2", "Collider"], data.Colliders.Select(c => c.Name));
    }

    [Fact]
    public void AddCollider_IsUnitSquareAroundCenter()
    {
        var data = new GameplayData();
        ColliderDef c = GameplayEditing.AddCollider(data, 3, -2);

        Assert.Equal((2.5f, -2.5f, 3.5f, -1.5f), (c.MinX, c.MinZ, c.MaxX, c.MaxZ));
    }

    [Fact]
    public void AddTrigger_TagIsMaxPlusOne()
    {
        var data = new GameplayData();
        TriggerDef first = GameplayEditing.AddTrigger(data, 0, 0);
        data.Triggers.Add(new TriggerDef { Name = "Door", Tag = 7 });
        TriggerDef next = GameplayEditing.AddTrigger(data, 4, 4);

        Assert.Equal(1, first.Tag);
        Assert.Equal(8, next.Tag);
        Assert.Equal([3f, 3f, 5f, 3f, 5f, 5f, 3f, 5f], next.Polygon);  // 2 x 2, 중심 (4, 4)
    }

    [Fact]
    public void SetPlayer_UsesNodeSpawn_KeepsExistingRadiusAndSpeed_RejectsChildNode()
    {
        var data = new GameplayData { Player = new PlayerDef { Node = "Old", Radius = 0.9f, Speed = 5f } };
        var hero = new SceneNode { Name = "Hero", Position = new Vec3(2, 1, -3) };

        GameplayEditing.SetPlayer(data, hero);

        PlayerDef player = data.Player!;
        Assert.Equal("Hero", player.Node);
        Assert.Equal(new Vec2(2, -3), player.Spawn);
        Assert.Equal((0.9f, 5f), (player.Radius, player.Speed));

        var doc = new SceneDocument();
        var parent = new SceneNode { Name = "P" };
        var child = new SceneNode { Name = "C" };
        doc.Add(parent);
        doc.Add(child, parent);
        Assert.Throws<InvalidOperationException>(() => GameplayEditing.SetPlayer(data, child));
        Assert.Equal("Hero", data.Player.Node);  // 거부되면 기존 플레이어는 그대로다
    }

    [Fact]
    public void SetPlayer_CreatesPlayerWhenNone()
    {
        var data = new GameplayData();
        GameplayEditing.SetPlayer(data, new SceneNode { Name = "Hero" });

        PlayerDef player = Assert.IsType<PlayerDef>(data.Player);
        Assert.Equal((0.4f, 3f), (player.Radius, player.Speed));  // PlayerDef 기본값
    }

    [Theory]
    [InlineData("0 0, 4 0, 4 4, 0 4")]
    [InlineData("0,0\n4,0\n4,4\n0,4")]
    [InlineData("  0 0;4 0;4 4;0 4  ")]
    [InlineData("0, 0\r\n4, 0\r\n4, 4\r\n0, 4\r\n")]
    public void TryParsePolygon_AcceptsAnySeparators(string text)
    {
        Assert.True(GameplayEditing.TryParsePolygon(text, out float[] polygon));
        Assert.Equal([0f, 0f, 4f, 0f, 4f, 4f, 0f, 4f], polygon);
    }

    [Theory]
    [InlineData("")]                      // 비어 있음
    [InlineData("0 0, 4 0")]              // 꼭짓점 2개
    [InlineData("0 0, 4 0, 4")]           // 숫자 개수가 홀수
    [InlineData("0 0, 4 0, 4 x")]         // 숫자가 아님
    [InlineData("0 0, 4 0, 4 NaN")]       // 유한하지 않음
    [InlineData("0 0, 4 0, 4 Infinity")]
    [InlineData("0 0, 4 0, 1e999 4")]     // float 범위 초과 -> 무한대
    public void TryParsePolygon_RejectsInvalidText(string text)
    {
        Assert.False(GameplayEditing.TryParsePolygon(text, out float[] polygon));
        Assert.Empty(polygon);
    }

    [Fact]
    public void FormatPolygon_RoundTripsExactly_IncludingNonDecimalFloats()
    {
        float[] original = [0.1f, -2.5f, 1f / 3f, 7f, 1e-5f, 12345.678f];
        string text = GameplayEditing.FormatPolygon(original);

        Assert.Equal(3, text.Split(Environment.NewLine).Length);  // 꼭짓점마다 한 줄
        Assert.True(GameplayEditing.TryParsePolygon(text, out float[] parsed));
        Assert.Equal(original, parsed);  // 정밀도 손실 없이 같은 비트
    }

    [Fact]
    public void TryParseNumbers_EmptyTextIsEmptyArray_BadTokenFails()
    {
        Assert.True(GameplayEditing.TryParseNumbers("  ", out float[] none));
        Assert.Empty(none);
        Assert.True(GameplayEditing.TryParseNumbers("1, 2.5 -3", out float[] three));
        Assert.Equal([1f, 2.5f, -3f], three);
        Assert.False(GameplayEditing.TryParseNumbers("1, abc", out _));
    }

    [Fact]
    public void Translate_MovesPolygonAndCollider_WithoutChangingOriginalArray()
    {
        float[] polygon = [0, 0, 2, 0, 2, 2];
        float[] moved = GameplayEditing.Translate(polygon, 1.5f, -1);

        Assert.Equal([1.5f, -1f, 3.5f, -1f, 3.5f, 1f], moved);
        Assert.Equal([0f, 0f, 2f, 0f, 2f, 2f], polygon);

        var collider = new ColliderDef { MinX = 0, MinZ = 0, MaxX = 1, MaxZ = 2 };
        GameplayEditing.Translate(collider, 3, 4);
        Assert.Equal((3f, 4f, 4f, 6f), (collider.MinX, collider.MinZ, collider.MaxX, collider.MaxZ));
    }

    [Fact]
    public void EditedGameplay_SurvivesSaveLoad_AndPlaysAtSpawn()
    {
        // 편집 로직만으로 만든 데이터가 저장 -> 로드 -> 플레이 시작까지 실제로 동작하는지 끝에서 끝으로 확인한다.
        var doc = new SceneDocument();
        var hero = new SceneNode { Name = "Hero", Position = new Vec3(5, 0.5f, 5) };
        doc.Add(hero);
        GameplayEditing.AddRoom(doc.Gameplay, new Vec3(5, 6, 12), new Vec3(5, 0, 5), 0.8f);
        GameplayEditing.AddTrigger(doc.Gameplay, 5, 5);
        GameplayEditing.AddCollider(doc.Gameplay, 8, 5);
        GameplayEditing.SetPlayer(doc.Gameplay, hero);

        string dir = Path.Combine(Path.GetTempPath(), "gge_edit_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            string path = Path.Combine(dir, "edited.scene.json");
            SceneSerializer.Save(doc, path);
            SceneDocument loaded = SceneSerializer.Load(path);

            Assert.Equal(doc.Gameplay.Rooms[0].Polygon, loaded.Gameplay.Rooms[0].Polygon);
            Assert.Equal(doc.Gameplay.Player!.Spawn, loaded.Gameplay.Player!.Spawn);

            using PlaySession play = PlaySession.Start(loaded);
            Assert.Equal(new Vec2(5, 5), play.Position);
            Assert.Equal("Room", play.ActiveRoomName);
            IReadOnlyList<PlayEvent> events = play.Step(0, 0, 0.033f);
            PlayEvent enter = Assert.Single(events);  // 스폰이 새 트리거 안이라 첫 Step에 Enter
            Assert.Equal((TriggerEventKind.Enter, 1), (enter.Kind, enter.Tag));
        }
        finally { Directory.Delete(dir, true); }
    }
}
