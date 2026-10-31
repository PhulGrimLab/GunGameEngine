using GunGame.Interop;
using GunGame.Scene;

namespace GunGame.Interop.Tests;

/// <summary>
/// 이름 있는 충돌체와 켜기/끄기(문): .ggs version 2 굽기, C# 래퍼의 오류 변환, 에디터 플레이 모드(PlaySession)와 엔진 런타임(Scene)이
/// 문을 열고 닫는 입력 대본에서 프레임마다 같은 위치를 내는지(교차 검증). 엔진 자체의 규칙은 engine_tests.cpp가 검증한다.
/// </summary>
public class ColliderTests
{
    private const float FrameSeconds = 1f / 30f;

    private static string SamplesDir()
    {
        for (string? dir = AppContext.BaseDirectory; dir is not null; dir = Path.GetDirectoryName(dir))
        {
            string candidate = Path.Combine(dir, "assets", "samples");
            if (Directory.Exists(candidate))
                return candidate;
        }
        throw new DirectoryNotFoundException("assets/samples not found above the test binaries");
    }

    /// <summary>플레이 샘플(Hall x -6..0, 스폰 (-4,0), 반지름 0.4, 속도 3)에 Hall 안쪽의 두 겹 문 "Gate"(x -1..-0.5, 이름이 같은 둘)를 더한다.</summary>
    private static SceneDocument GateDocument()
    {
        SceneDocument doc = PlaySamples.Load();
        doc.Gameplay.Colliders.Add(new ColliderDef { Name = "Gate", MinX = -1f, MinZ = -4f, MaxX = -0.75f, MaxZ = 4f });
        doc.Gameplay.Colliders.Add(new ColliderDef { Name = "Gate", MinX = -0.75f, MinZ = -4f, MaxX = -0.5f, MaxZ = 4f });
        return doc;
    }

    private static string Bake(TestAssets assets, SceneDocument doc)
    {
        string path = assets.Path_("scene.ggs");
        SceneBaker.Bake(doc, path);
        return path;
    }

    // ---- 굽기 ----
    [Fact]
    public void Baker_WritesVersion2_WithColliderNames_IncludingNonAscii()
    {
        SceneDocument doc = GateDocument();
        doc.Gameplay.Colliders.First(c => c.Name == "WallEast").Name = "동쪽 벽";
        byte[] bytes = SceneBaker.BakeToBytes(doc);
        Assert.Equal(2u, BitConverter.ToUInt32(bytes, 4));
        Assert.Equal(SceneBaker.Version, BitConverter.ToUInt32(bytes, 4));

        using var assets = new TestAssets();
        string path = assets.Path_("names.ggs");
        File.WriteAllBytes(path, bytes);
        using var renderer = new EngineRenderer(64, 48, useWarp: true);
        using EngineScene scene = EngineScene.Load(renderer, path);
        Assert.Equal(2, scene.SetColliderEnabled("Gate", false));        // 이름이 같은 둘
        Assert.Equal(1, scene.SetColliderEnabled("Pillar", false));
        Assert.Equal(1, scene.SetColliderEnabled("동쪽 벽", false));       // 한글 이름이 네이티브 경계를 넘는다
        Assert.Throws<EngineException>(() => scene.SetColliderEnabled("WallEast", false));   // 바뀐 이름
    }

    // ---- C# 래퍼 ----
    [Fact]
    public void EngineScene_SetColliderEnabled_RejectsEmptyNullAndUnknownNames_WithReasons()
    {
        using var assets = new TestAssets();
        using var renderer = new EngineRenderer(64, 48, useWarp: true);
        using EngineScene scene = EngineScene.Load(renderer, Bake(assets, GateDocument()));
        Assert.Contains("must not be empty", Assert.Throws<EngineException>(() => scene.SetColliderEnabled("", false)).Message);
        Assert.Contains("must not be empty", Assert.Throws<EngineException>(() => scene.SetColliderEnabled(null!, false)).Message);
        Assert.Contains("no collider named 'Nope'", Assert.Throws<EngineException>(() => scene.SetColliderEnabled("Nope", false)).Message);
        Assert.Throws<EngineException>(() => scene.SetColliderEnabled("gate", false));   // 대소문자 구분
        Assert.Equal(2, scene.SetColliderEnabled("Gate", false));
        Assert.Equal(2, scene.SetColliderEnabled("Gate", false));                         // 이미 꺼져 있어도 같은 개수(멱등)
    }

    [Fact]
    public void EngineWorld_ColliderToggle_FollowsTheEngineRules()
    {
        using var world = new EngineWorld();
        int id = world.AddBoxCollider(2, -5, 3, 5);
        Assert.True(world.IsColliderEnabled(id));
        Assert.Equal(1.5f, world.MoveCircle(new Vec2(0, 0), new Vec2(5, 0), 0.5f).X, 2);
        world.SetColliderEnabled(id, false);
        Assert.False(world.IsColliderEnabled(id));
        Assert.Equal(5f, world.MoveCircle(new Vec2(0, 0), new Vec2(5, 0), 0.5f).X, 3);
        world.SetColliderEnabled(id, true);
        Assert.Equal(1.5f, world.MoveCircle(new Vec2(2.3f, 0), new Vec2(0, 0), 0.5f).X, 2);   // 켜는 순간 안이면 이동량 0이어도 밀려난다

        foreach (int unknown in new[] { 0, -1, id + 1 })
        {
            Assert.Contains("unknown collider id", Assert.Throws<EngineException>(() => world.SetColliderEnabled(unknown, false)).Message);
            Assert.Throws<EngineException>(() => world.IsColliderEnabled(unknown));
        }
    }

    [Fact]
    public void PlaySession_SetColliderEnabled_FollowsTheSameNameRules_AndPushesOutOnZeroInput()
    {
        using PlaySession session = PlaySession.Start(GateDocument());
        Assert.Throws<ArgumentNullException>(() => session.SetColliderEnabled(null!, false));
        Assert.Throws<ArgumentException>(() => session.SetColliderEnabled("", false));
        Assert.Contains("no collider named 'Nope'", Assert.Throws<ArgumentException>(() => session.SetColliderEnabled("Nope", false)).Message);
        Assert.Throws<ArgumentException>(() => session.SetColliderEnabled("gate", false));
        Assert.Equal(2, session.SetColliderEnabled("Gate", false));
        Assert.Equal(2, session.SetColliderEnabled("Gate", false));

        for (int i = 0; i < 32; i++)
            session.Step(1, 0, FrameSeconds);                      // 스폰 x=-4에서 초당 3 x 1/30 = 0.1씩: 열린 문 안(x -1..-0.5)까지 걸어 들어간다
        Assert.InRange(session.Position.X, -1f, -0.5f);
        float insideX = session.Position.X;
        Assert.Equal(2, session.SetColliderEnabled("Gate", true));
        session.Step(0, 0, FrameSeconds);                          // 입력이 0인 프레임에서도 밀려난다
        Assert.NotEqual(insideX, session.Position.X);
        Assert.True(session.Position.X <= -1.4f + 1e-3f || session.Position.X >= -0.1f - 1e-3f, $"x {session.Position.X}");
    }

    // ---- 교차 검증: 에디터 플레이 모드(C# PlaySession) = 엔진 런타임(C++ Scene) ----
    private enum Door { None, Open, Close }

    /// <summary>(입력 x, 입력 y, 이번 프레임 시작 전에 할 일). 오른쪽으로 걷다 닫힌 문에 막힘(x=-1.4) -> 열고 문 안(x=-0.8)까지 걸어 들어가 서 있음
    /// -> 안에 선 채로 닫음(입력 0인 프레임에 밀려남, 오른쪽 면 바깥 x=-0.1) -> Annex로 걸어감(카메라 컷) -> 카메라 기준 "위"(-X)로 돌아오다 반대편에서 닫힌 문에 막힘.</summary>
    private static IEnumerable<(float X, float Y, Door Door)> DoorScript()
    {
        for (int i = 0; i < 40; i++) yield return (1, 0, Door.None);     // x -4 -> -1.4에서 막혀 14프레임쯤 제자리
        yield return (1, 0, Door.Open);
        for (int i = 0; i < 6; i++) yield return (1, 0, Door.None);      // 열린 문 안(약 -0.8)까지
        for (int i = 0; i < 2; i++) yield return (0, 0, Door.None);
        yield return (0, 0, Door.Close);                                  // 안에 서 있는 채로 닫는다: 입력 0 프레임에서 밀려나야 한다
        for (int i = 0; i < 3; i++) yield return (0, 0, Door.None);
        for (int i = 0; i < 30; i++) yield return (1, 0, Door.None);     // 밀려난 쪽(오른쪽)에서 Annex 쪽으로 계속 걷는다(방 카메라 컷)
        for (int i = 0; i < 3; i++) yield return (0, 0, Door.None);
        for (int i = 0; i < 45; i++) yield return (0, 1, Door.None);     // Annex 카메라는 -X를 보므로 "위" 입력 = -X: 돌아오다 닫힌 문 오른쪽 면에서 막힌다
    }

    [Fact]
    public void DoorScript_PlayModeAndEngineRuntime_AgreeOnEveryFrame_IncludingThePushOutOnZeroInput()
    {
        using var assets = new TestAssets();
        SceneDocument doc = GateDocument();
        using PlaySession session = PlaySession.Start(doc);
        using var renderer = new EngineRenderer(64, 48, useWarp: true);
        using EngineScene scene = EngineScene.Load(renderer, Bake(assets, doc));
        session.Step(0, 0, 0);
        scene.Step(0, 0, 0);

        int frame = 0, pushOuts = 0, blocked = 0;
        float previousX = session.Position.X;
        bool closedAndStanding = false;
        foreach ((float x, float y, Door door) in DoorScript())
        {
            if (door != Door.None)
            {
                bool enable = door == Door.Close;
                Assert.Equal(2, session.SetColliderEnabled("Gate", enable));
                Assert.Equal(2, scene.SetColliderEnabled("Gate", enable));
                closedAndStanding = enable && x == 0 && y == 0;
            }
            frame++;
            session.Step(x, y, FrameSeconds);
            scene.Step(x, y, FrameSeconds);

            (Vec2 position, float yaw) = scene.Player!.Value;
            Assert.True(MathF.Abs(position.X - session.Position.X) < 1e-4f && MathF.Abs(position.Z - session.Position.Z) < 1e-4f,
                        $"frame {frame}: runtime ({position.X}, {position.Z}) vs play mode ({session.Position.X}, {session.Position.Z})");
            Assert.True(MathF.Abs(yaw - session.Yaw) < 1e-4f, $"frame {frame}: yaw {yaw} vs {session.Yaw}");
            Assert.Equal(session.ActiveRoomName, scene.ActiveRoomName);

            if (closedAndStanding)
            {
                // 닫는 프레임: 입력이 0인데도 위치가 바뀌었다(= 양쪽 모두 입력 0 프레임에 충돌 해결을 한다).
                Assert.True(MathF.Abs(session.Position.X - previousX) > 0.05f, $"frame {frame}: no push-out");
                pushOuts++;
                closedAndStanding = false;
            }
            if ((x != 0 || y != 0) && MathF.Abs(session.Position.X - previousX) < 1e-3f)
                blocked++;   // 입력이 있는데 제자리 = 닫힌 문에 막힘
            previousX = session.Position.X;
        }
        Assert.Equal(1, pushOuts);
        Assert.True(blocked >= 20, $"the closed gate should have blocked the walk twice (blocked frames: {blocked})");
        Assert.Equal("Hall", session.ActiveRoomName);   // 마지막엔 문 오른쪽 면(x=-0.5)에서 반지름만큼 떨어진 곳에서 막혀 있다
        Assert.InRange(session.Position.X, -0.11f, -0.09f);
    }
}
