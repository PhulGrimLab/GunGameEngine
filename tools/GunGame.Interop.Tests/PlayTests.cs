using GunGame.Interop;
using GunGame.Scene;

namespace GunGame.Interop.Tests;

internal static class PlaySamples
{
    public static string PlayScenePath()
    {
        for (string? dir = AppContext.BaseDirectory; dir is not null; dir = Path.GetDirectoryName(dir))
        {
            string candidate = Path.Combine(dir, "assets", "samples", "play.scene.json");
            if (File.Exists(candidate))
                return candidate;
        }
        throw new FileNotFoundException("assets/samples/play.scene.json not found");
    }

    public static SceneDocument Load() => SceneSerializer.Load(PlayScenePath());

    /// <summary>방 하나(0..4 정사각형, 북쪽 카메라)와 플레이어 노드 "Hero"가 있는 최소 문서.</summary>
    public static SceneDocument Minimal(Action<SceneDocument>? configure = null)
    {
        var doc = new SceneDocument();
        doc.Add(new SceneNode { Name = "Hero", Position = new Vec3(2, 0, 2) });
        doc.Gameplay.Rooms.Add(new RoomDef { Name = "Room", Polygon = [0, 0, 4, 0, 4, 4, 0, 4], Eye = new Vec3(2, 5, 9), Target = new Vec3(2, 0, 2) });
        doc.Gameplay.Player = new PlayerDef { Node = "Hero" };
        configure?.Invoke(doc);
        return doc;
    }
}

public class GameplaySerializationTests
{
    [Fact]
    public void SampleScene_LoadsGameplayData()
    {
        SceneDocument doc = PlaySamples.Load();
        GameplayData g = doc.Gameplay;
        Assert.Equal(["Hall", "Annex"], g.Rooms.Select(r => r.Name));
        Assert.Equal(8, g.Rooms[0].Polygon.Length);
        Assert.Equal(new Vec3(-3, 6, 9), g.Rooms[0].Eye);
        Assert.Equal(new Vec3(3, 0, 0), g.Rooms[1].Target);
        Assert.Equal(5, g.Colliders.Count);
        Assert.Equal(new[] { 1, 7 }, g.Triggers.Select(t => t.Tag));
        PlayerDef player = Assert.IsType<PlayerDef>(g.Player);
        Assert.Equal("Hero", player.Node);
        Assert.Equal(new Vec2(-4, 0), player.Spawn);
        Assert.Equal(0.4f, player.Radius);
        Assert.Equal(3f, player.Speed);
    }

    [Fact]
    public void Gameplay_RoundTripsThroughSaveAndLoad()
    {
        using var assets = new TestAssets();
        SceneDocument doc = PlaySamples.Minimal(d =>
        {
            d.Gameplay.Colliders.Add(new ColliderDef { Name = "Crate", MinX = 1, MinZ = 1, MaxX = 2, MaxZ = 3 });
            d.Gameplay.Triggers.Add(new TriggerDef { Name = "Switch", Polygon = [1, 1, 3, 1, 3, 3], Tag = 42 });
            d.Gameplay.Player = new PlayerDef { Node = "Hero", Spawn = new Vec2(1.5f, 2.5f), Radius = 0.25f, Speed = 5 };
        });
        string path = assets.Path_("g.scene.json");
        SceneSerializer.Save(doc, path);
        GameplayData loaded = SceneSerializer.Load(path).Gameplay;

        RoomDef room = Assert.Single(loaded.Rooms);
        Assert.Equal("Room", room.Name);
        Assert.Equal(new float[] { 0, 0, 4, 0, 4, 4, 0, 4 }, room.Polygon);
        Assert.Equal(new Vec3(2, 5, 9), room.Eye);
        Assert.Equal(new Vec3(2, 0, 2), room.Target);
        Assert.Equal(0.8f, room.Fov);
        ColliderDef collider = Assert.Single(loaded.Colliders);
        Assert.Equal(("Crate", 1f, 1f, 2f, 3f), (collider.Name, collider.MinX, collider.MinZ, collider.MaxX, collider.MaxZ));
        TriggerDef trigger = Assert.Single(loaded.Triggers);
        Assert.Equal(("Switch", 42), (trigger.Name, trigger.Tag));
        Assert.Equal(new float[] { 1, 1, 3, 1, 3, 3 }, trigger.Polygon);
        PlayerDef player = Assert.IsType<PlayerDef>(loaded.Player);
        Assert.Equal(("Hero", new Vec2(1.5f, 2.5f), 0.25f, 5f), (player.Node, player.Spawn!.Value, player.Radius, player.Speed));
    }

    [Fact]
    public void SceneWithoutGameplay_StaysCompatibleBothWays()
    {
        using var assets = new TestAssets();
        // 게임플레이 섹션이 없는 기존 파일은 그대로 열린다.
        string old = assets.Path_("old.scene.json");
        File.WriteAllText(old, """{"version":1,"nodes":[{"name":"A"}]}""");
        Assert.True(SceneSerializer.Load(old).Gameplay.IsEmpty);

        // 게임플레이가 비어 있으면 파일에도 쓰지 않는다(기존 도구가 보는 파일이 바뀌지 않는다).
        string saved = assets.Path_("saved.scene.json");
        SceneSerializer.Save(SceneSerializer.Load(old), saved);
        Assert.DoesNotContain("gameplay", File.ReadAllText(saved));
    }

    [Theory]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"rooms":[null]}}""", "null entry")]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"rooms":[{"name":"R","polygon":[0,0,1,0,1,1]}]}}""", "camera")]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"rooms":[{"name":"R","polygon":[0,0,1,0,1],"camera":{"eye":[0,1,0],"target":[0,0,0]}}]}}""", "polygon")]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"rooms":[{"name":"R","polygon":[0,0,1,0],"camera":{"eye":[0,1,0],"target":[0,0,0]}}]}}""", "polygon")]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"rooms":[{"name":"R","polygon":[0,0,1,0,1,1],"camera":{"eye":[0,1],"target":[0,0,0]}}]}}""", "eye")]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"rooms":[{"name":"R","polygon":[0,0,1,0,1,1],"camera":{"target":[0,0,0]}}]}}""", "eye")]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"colliders":[{"name":"C","min":[0,0,0],"max":[1,1]}]}}""", "min")]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"colliders":[null]}}""", "null entry")]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"triggers":[{"name":"T"}]}}""", "polygon")]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"triggers":[null]}}""", "null entry")]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"player":{}}}""", "node")]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"player":{"node":"H","radius":0}}}""", "radius")]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"player":{"node":"H","speed":-1}}}""", "speed")]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"player":{"node":"H","spawn":[1]}}}""", "spawn")]
    [InlineData("""{"version":1,"nodes":[],"gameplay":{"rooms":[{"name":"R","polygon":[0,0,1,0,1,1],"camera":{"eye":[0,1,0],"target":[0,0,0],"fov":1e999}}]}}""", "")]
    public void HostileGameplayJson_IsAFormatErrorWithAUsefulMessage(string json, string expectedFragment)
    {
        using var assets = new TestAssets();
        string path = assets.Path_("hostile.scene.json");
        File.WriteAllText(path, json);
        SceneFormatException ex = Assert.Throws<SceneFormatException>(() => SceneSerializer.Load(path));
        Assert.Contains(expectedFragment, ex.Message, StringComparison.OrdinalIgnoreCase);
    }
}

public class EngineWorldTests
{
    private static readonly float[] Square = [0, 0, 4, 0, 4, 4, 0, 4];
    private static readonly RoomCameraData Camera = new(new Vec3(2, 5, 9), new Vec3(2, 0, 2), 0.8f);

    [Fact]
    public void RoomsCollidersAndTriggers_WorkThroughTheWrapper()
    {
        using var world = new EngineWorld();
        int room = world.AddRoom(Square, Camera);
        Assert.Equal(1, room);
        Assert.Equal(1, world.FindRoom(1, 1));
        Assert.Equal(0, world.FindRoom(9, 9));
        Assert.Equal(0, world.ActiveRoom);
        Assert.Equal(1, world.UpdateActiveRoom(1, 1));
        Assert.Equal(Camera, world.GetRoomCamera(room));

        world.AddBoxCollider(5, -5, 6, 9);
        Vec2 blocked = world.MoveCircle(new Vec2(3, 2), new Vec2(10, 0), 0.5f);  // 벽(x=5) 앞 4.5에서 멈춘다
        Assert.Equal(4.5f, blocked.X, 2);
        Assert.Equal(2f, blocked.Z, 2);

        Vec2 move = world.MoveInput(0, 1, 2, 0.5f);  // 카메라가 -Z를 보므로 앞 = -Z
        Assert.Equal(0f, move.X, 3);
        Assert.Equal(-1f, move.Z, 3);

        int trigger = world.AddTrigger([2, 2, 3, 2, 3, 3, 2, 3], tag: 9);
        Assert.Equal(1, world.UpdateTriggers(2.5f, 2.5f));
        Assert.True(world.IsInsideTrigger(trigger));
        Assert.True(world.TryPollTriggerEvent(out TriggerEvent enter));
        Assert.Equal(new TriggerEvent(trigger, TriggerEventKind.Enter, 9), enter);
        Assert.False(world.TryPollTriggerEvent(out _));

        world.SetTriggerEnabled(trigger, false);
        Assert.Equal(1, world.UpdateTriggers(2.5f, 2.5f));  // 안에서 끄면 Exit
        Assert.True(world.TryPollTriggerEvent(out TriggerEvent exit));
        Assert.Equal(TriggerEventKind.Exit, exit.Kind);
    }

    [Fact]
    public void BadInput_IsReportedAsExceptions()
    {
        using var world = new EngineWorld();
        Assert.Throws<ArgumentException>(() => world.AddRoom([0, 0, 1, 0, 1], Camera));      // 홀수 길이
        Assert.Throws<ArgumentException>(() => world.AddTrigger([0, 0, 1], 1));
        Assert.Throws<EngineException>(() => world.AddRoom([0, 0, 4, 2, 4, 0, 0, 1], Camera));  // 자기 교차
        Assert.Throws<EngineException>(() => world.GetRoomCamera(99));
        Assert.Throws<EngineException>(() => world.MoveInput(0, 1, 1, 1));                      // 활성 방 없음
        Assert.Throws<EngineException>(() => world.UpdateTriggers(float.NaN, 0));
        Assert.Throws<EngineException>(() => world.IsInsideTrigger(5));
        Assert.Throws<EngineException>(() => world.MoveCircle(new Vec2(0, 0), new Vec2(1, 0), 0));
    }

    [Fact]
    public void UsingADisposedWorld_Throws()
    {
        var world = new EngineWorld();
        world.Dispose();
        world.Dispose();  // 두 번 해제해도 안전
        Assert.Throws<ObjectDisposedException>(() => world.FindRoom(0, 0));
    }
}

public class PlaySessionTests
{
    private static PlayException StartFails(SceneDocument doc) => Assert.Throws<PlayException>(() => PlaySession.Start(doc));

    [Fact]
    public void Start_ReportsWhyAScenesCannotBePlayed()
    {
        Assert.Contains("gameplay.player", StartFails(PlaySamples.Minimal(d => d.Gameplay.Player = null)).Message);
        Assert.Contains("rooms", StartFails(PlaySamples.Minimal(d => d.Gameplay.Rooms.Clear())).Message);
        Assert.Contains("'Nobody'", StartFails(PlaySamples.Minimal(d => d.Gameplay.Player = new PlayerDef { Node = "Nobody" })).Message);
        Assert.Contains("outside every room", StartFails(PlaySamples.Minimal(d => d.Gameplay.Player!.Spawn = new Vec2(10, 10))).Message);

        SceneDocument nested = PlaySamples.Minimal(d =>
        {
            d.Remove(d.Roots[0]);
            var group = new SceneNode { Name = "Group" };
            d.Add(group);
            d.Add(new SceneNode { Name = "Hero" }, group);
        });
        Assert.Contains("root node", StartFails(nested).Message);

        PlayException bad = StartFails(PlaySamples.Minimal(d => d.Gameplay.Rooms.Add(
            new RoomDef { Name = "Bad", Polygon = [0, 0, 4, 2, 4, 0, 0, 1], Eye = new Vec3(0, 5, 5), Target = new Vec3(0, 0, 0) })));
        Assert.StartsWith("room 'Bad'", bad.Message);  // 어느 영역이 문제인지 이름으로 알려 준다
        Assert.IsType<EngineException>(bad.InnerException);

        PlayException badCollider = StartFails(PlaySamples.Minimal(d => d.Gameplay.Colliders.Add(new ColliderDef { Name = "Flat", MinX = 1, MaxX = 1, MinZ = 0, MaxZ = 1 })));
        Assert.StartsWith("collider 'Flat'", badCollider.Message);
    }

    [Fact]
    public void Step_MovesRelativeToTheRoomCamera_WithoutTouchingTheDocument()
    {
        SceneDocument doc = PlaySamples.Load();
        SceneNode hero = doc.Roots.Single(n => n.Name == "Hero");
        Vec3 before = hero.Position;
        using PlaySession play = PlaySession.Start(doc);

        Assert.Equal(new Vec2(-4, 0), play.Position);
        play.Step(0, 1, 0.5f);  // 홀 카메라는 -Z를 본다: 앞 = -Z, 속도 3 x 0.5 = 1.5
        Assert.Equal(-4f, play.Position.X, 3);
        Assert.Equal(-1.5f, play.Position.Z, 3);
        play.Step(1, 0, 0.5f);  // 오른쪽 = +X
        Assert.Equal(-2.5f, play.Position.X, 3);
        Assert.Equal(new Vec3(-2.5f, before.Y, -1.5f), play.PlayerPosition);  // 높이는 노드의 값을 유지한다

        Assert.Equal(before, hero.Position);  // 문서는 그대로다
        Assert.Equal(0f, hero.Rotation.Y);
    }

    [Fact]
    public void Yaw_FollowsTheIntendedDirection()
    {
        using PlaySession play = PlaySession.Start(PlaySamples.Load());
        play.Step(1, 0, 0.1f);  // +X
        Assert.Equal(MathF.PI / 2, play.Yaw, 3);
        play.Step(0, 1, 0.1f);  // -Z
        Assert.Equal(MathF.PI, MathF.Abs(play.Yaw), 3);
        float held = play.Yaw;
        play.Step(0, 0, 0.1f);  // 입력이 없으면 마지막 방향을 유지한다
        Assert.Equal(held, play.Yaw);
        Assert.Equal(play.Yaw, play.PlayerRotation.Y);
    }

    [Fact]
    public void WalkingIntoThePillar_StopsAtItsEdge()
    {
        SceneDocument doc = PlaySamples.Load();
        doc.Gameplay.Player!.Spawn = new Vec2(-2, 0);  // 기둥 바로 위쪽(z=0)에서 시작
        using PlaySession play = PlaySession.Start(doc);
        for (int i = 0; i < 120; i++) play.Step(0, -1, 1 / 30f);  // 아래 입력 = +Z(화면 아래쪽)로 계속 걷는다
        Assert.Equal(-2f, play.Position.X, 2);
        Assert.Equal(1.5f - 0.4f, play.Position.Z, 2);  // 기둥 면(z=1.5)에서 반지름만큼 앞
    }

    [Fact]
    public void CrossingIntoTheNextRoom_CutsTheCamera_ButKeepsTheHeldDirection()
    {
        using PlaySession play = PlaySession.Start(PlaySamples.Load());
        Assert.Equal(0, play.ActiveRoomIndex);
        Assert.Equal(new Vec3(-3, 6, 9), play.Camera.Eye);

        float lastX = play.Position.X;
        bool cut = false;
        for (int i = 0; i < 90 && !cut; i++)  // 오른쪽을 누른 채 걷는다: 홀 기준 +X
        {
            play.Step(1, 0, 1 / 30f);
            cut = play.ActiveRoomIndex == 1;
            lastX = play.Position.X;
        }
        Assert.True(cut, "the player should have crossed into the Annex");
        Assert.Equal("Annex", play.ActiveRoomName);
        Assert.Equal(new Vec3(10, 4, 0), play.Camera.Eye);  // 방 2의 카메라로 컷

        // 별관 카메라는 -X를 본다(오른쪽 = -Z). 누른 채 계속 걸으면 컷 직전 기준(+X)이 유지되어 앞으로 계속 나아간다.
        float z = play.Position.Z;
        for (int i = 0; i < 10; i++) play.Step(1, 0, 1 / 30f);
        Assert.True(play.Position.X > lastX + 0.5f, "should keep walking +X after the cut");
        Assert.Equal(z, play.Position.Z, 2);

        play.Step(0, 0, 1 / 30f);   // 놓으면 새 방 기준으로 갱신된다
        float x = play.Position.X;
        play.Step(1, 0, 0.1f);      // 이제 오른쪽 = -Z
        Assert.Equal(x, play.Position.X, 2);
        Assert.True(play.Position.Z < z - 0.2f);
    }

    [Fact]
    public void SpawningInsideATrigger_StillReportsEnterWhenThePlayerLeavesOnTheFirstStep()
    {
        using PlaySession play = PlaySession.Start(PlaySamples.Load());  // StartPad(-5..-3) 안에서 시작
        IReadOnlyList<PlayEvent> events = play.Step(1, 0, 1.0f);          // 한 번에 3칸을 가서 영역 밖(x=-1)으로 나간다
        Assert.Equal(-1f, play.Position.X, 2);
        Assert.Equal(
            [new PlayEvent("StartPad", 1, TriggerEventKind.Enter), new PlayEvent("StartPad", 1, TriggerEventKind.Exit)],
            events);  // 시작 시점의 Enter가 사라지지 않는다
    }

    [Fact]
    public void Triggers_ReportEnterAndExitByNameInOrder()
    {
        using PlaySession play = PlaySession.Start(PlaySamples.Load());
        var log = new List<string>();
        void Collect(IReadOnlyList<PlayEvent> events) => log.AddRange(events.Select(e => $"{e.Kind} {e.TriggerName}#{e.Tag}"));

        Collect(play.Step(0, 0, 1 / 30f));  // 시작 위치가 StartPad 안: 첫 Step에 Enter
        Assert.Equal(["Enter StartPad#1"], log);

        for (int i = 0; i < 300 && play.Position.X < 5; i++)
            Collect(play.Step(1, 0, 1 / 30f));
        Assert.Equal(["Enter StartPad#1", "Exit StartPad#1", "Enter Door#7", "Exit Door#7"], log);
    }
}

public class GameplayOverlayTests
{
    private static byte[] Frame(EngineRenderer r, Vec3 eye)
    {
        r.SetCamera(eye, new Vec3(0, 0, 0), 0.8f);
        r.Render();
        return r.Pixels.ToArray();
    }

    [Fact]
    public void Show_CreatesOneObjectPerEdgeAndMarker_AndHideRemovesThem()
    {
        using var r = new EngineRenderer(160, 120, useWarp: true);
        using var overlay = new GameplayOverlay(r);
        GameplayData g = PlaySamples.Load().Gameplay;

        Assert.False(overlay.Visible);
        overlay.Show(g);
        // 방 2개(변 4 + 카메라 표식 1) + 트리거 2개(변 4) + 충돌체 5개(변 4) + 스폰 표식 1
        Assert.Equal(2 * 5 + 2 * 4 + 5 * 4 + 1, overlay.ObjectCount);
        Assert.True(overlay.Visible);

        overlay.Show(g);  // 다시 그려도 쌓이지 않는다
        Assert.Equal(2 * 5 + 2 * 4 + 5 * 4 + 1, overlay.ObjectCount);

        overlay.Hide();
        Assert.Equal(0, overlay.ObjectCount);
        Assert.False(overlay.Visible);
    }

    [Fact]
    public void OverlayChangesTheFrame_AndHidingRestoresIt()
    {
        using var r = new EngineRenderer(160, 120, useWarp: true);
        using var overlay = new GameplayOverlay(r);
        Vec3 eye = new(0, 14, 10);
        byte[] empty = Frame(r, eye);

        overlay.Show(PlaySamples.Load().Gameplay);
        Assert.NotEqual(empty, Frame(r, eye));
        overlay.Hide();
        Assert.Equal(empty, Frame(r, eye));
    }

    [Fact]
    public void Segments_AreDrawnAlongTheirEdges_NotAlongTheMirrorDiagonal()
    {
        // 직각삼각형 영역 (-3,-3) (3,-3) (3,3): 빗변은 화면에서 왼쪽 위 -> 오른쪽 아래로 가는 "\" 방향이어야 한다.
        // 회전 부호가 틀리면 반대쪽 대각선 "/"에 그려진다.
        var data = new GameplayData();
        data.Triggers.Add(new TriggerDef { Name = "T", Polygon = [-3, -3, 3, -3, 3, 3] });

        const int width = 320, height = 240;
        using var r = new EngineRenderer(width, height, useWarp: true);
        using var overlay = new GameplayOverlay(r);
        overlay.Show(data);
        var eye = new Vec3(0, 10, 0.001f);  // 거의 수직으로 내려다본다: 화면 위쪽 = -Z
        byte[] pixels = Frame(r, eye);

        (int X, int Y) Project(float x, float z)
        {
            float half = 10 * MathF.Tan(0.4f);  // 깊이 10에서 화면 절반 높이
            float ndcX = x / (half * width / height), ndcY = -z / half;
            return ((int)((ndcX * 0.5f + 0.5f) * width), (int)((1 - (ndcY * 0.5f + 0.5f)) * height));
        }

        bool GreenNear((int X, int Y) p, int radius = 3)
        {
            for (int dy = -radius; dy <= radius; dy++)
                for (int dx = -radius; dx <= radius; dx++)
                {
                    int x = p.X + dx, y = p.Y + dy;
                    if (x < 0 || y < 0 || x >= width || y >= height) continue;
                    int i = (y * width + x) * 4;
                    byte b = pixels[i], g = pixels[i + 1], red = pixels[i + 2];
                    if (g > 150 && red < 120 && b < 140) return true;  // 트리거 색(초록)
                }
            return false;
        }

        Assert.True(GreenNear(Project(1.5f, 1.5f)), "hypotenuse midpoint should be drawn");
        Assert.True(GreenNear(Project(-1.5f, -1.5f)), "hypotenuse should extend to the upper-left");
        Assert.True(GreenNear(Project(2.7f, 2.7f)), "hypotenuse should reach its lower-right end");  // 선 길이가 짧아지면 끝쪽이 비는 것을 잡는다
        Assert.True(GreenNear(Project(-2.7f, -2.7f)), "hypotenuse should reach its upper-left end");
        Assert.True(GreenNear(Project(0, -3)), "top edge");
        Assert.True(GreenNear(Project(3, 0)), "right edge");
        Assert.False(GreenNear(Project(-1.5f, 1.5f)), "mirror diagonal must stay empty");
        Assert.False(GreenNear(Project(1.5f, -1.5f)), "mirror diagonal must stay empty");
    }

    [Fact]
    public void Dispose_ReleasesItsEngineResources()
    {
        using var r = new EngineRenderer(64, 48, useWarp: true);
        var overlay = new GameplayOverlay(r);
        overlay.Show(PlaySamples.Load().Gameplay);
        overlay.Dispose();  // 오브젝트를 먼저 없애고 메시를 해제한다: 사용 중 오류 없이 끝나야 한다
        Assert.Equal(0, overlay.ObjectCount);
        overlay.Dispose();  // 두 번 해제해도 안전
    }
}
