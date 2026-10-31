using GunGame.Interop;
using GunGame.Scene;

namespace GunGame.Interop.Tests;

/// <summary>
/// 구운 씬 런타임의 쓰기 API(스크립트가 쓰는 것): 이름으로 트리거 켜기/끄기, 플레이어 텔레포트. 엔진 자체의 규칙은
/// engine_tests.cpp가 검증하고, 여기서는 C# 래퍼의 오류 변환, 문자열(UTF-8) 마샬링, 수명 관리를 본다.
/// </summary>
public class SceneWriteApiTests
{
    private static (TestAssets Assets, EngineRenderer Renderer, EngineScene Scene) LoadSample(Action<SceneDocument>? edit = null)
    {
        var assets = new TestAssets();
        SceneDocument doc = PlaySamples.Load();
        edit?.Invoke(doc);
        string path = assets.Path_("scene.ggs");
        SceneBaker.Bake(doc, path);
        var renderer = new EngineRenderer(96, 72, useWarp: true);
        return (assets, renderer, EngineScene.Load(renderer, path));
    }

    private static string Drain(EngineScene scene)
    {
        var events = new List<string>();
        while (scene.TryPollEvent(out SceneEventData e))
            events.Add($"{e.Kind} {e.TriggerName} {e.Tag}");
        return string.Join(", ", events);
    }

    [Fact]
    public void SetTriggerEnabled_StopsAndRestartsEvents_OnTheNextStep()
    {
        var (assets, renderer, scene) = LoadSample();
        using (assets) using (renderer) using (scene)
        {
            Assert.Equal("Enter StartPad 1", Drain(scene));
            Assert.Equal(1, scene.SetTriggerEnabled("StartPad", false));
            Assert.Equal("", Drain(scene));  // 이벤트는 다음 Step에서
            scene.Step(0, 0, 0);
            Assert.Equal("Exit StartPad 1", Drain(scene));
            Assert.Equal(1, scene.SetTriggerEnabled("StartPad", true));
            scene.Step(0, 0, 0);
            Assert.Equal("Enter StartPad 1", Drain(scene));
        }
    }

    [Fact]
    public void SetTriggerEnabled_UnknownName_ThrowsWithTheReason_AndChangesNothing()
    {
        var (assets, renderer, scene) = LoadSample();
        using (assets) using (renderer) using (scene)
        {
            Drain(scene);
            EngineException ex = Assert.Throws<EngineException>(() => scene.SetTriggerEnabled("Nope", false));
            Assert.Contains("no trigger named 'Nope'", ex.Message);
            Assert.Throws<EngineException>(() => scene.SetTriggerEnabled("startpad", false));  // 대소문자 구분
            scene.Step(0, 0, 0);
            Assert.Equal("", Drain(scene));
        }
    }

    [Fact]
    public void SetTriggerEnabled_NonAsciiNames_SurviveTheNativeBoundary()
    {
        const string name = "시작 패드 ✓";
        var (assets, renderer, scene) = LoadSample(doc => doc.Gameplay.Triggers.First(t => t.Name == "StartPad").Name = name);
        using (assets) using (renderer) using (scene)
        {
            Assert.Equal($"Enter {name} 1", Drain(scene));
            Assert.Equal(1, scene.SetTriggerEnabled(name, false));
            scene.Step(0, 0, 0);
            Assert.Equal($"Exit {name} 1", Drain(scene));
        }
    }

    [Fact]
    public void TeleportPlayer_MovesToTheOtherRoomAtOnce_KeepingTheFacing()
    {
        var (assets, renderer, scene) = LoadSample();
        using (assets) using (renderer) using (scene)
        {
            Drain(scene);
            scene.TeleportPlayer(4, 0);
            (Vec2 position, float yaw) = scene.Player!.Value;
            Assert.Equal(new Vec2(4, 0), position);
            Assert.Equal(0f, yaw);
            Assert.Equal("Annex", scene.ActiveRoomName);
            Assert.Equal("Exit StartPad 1, Enter Door 7", Drain(scene));

            scene.TeleportPlayer(-4, 0);
            Assert.Equal("Hall", scene.ActiveRoomName);
            Assert.Equal("Exit Door 7, Enter StartPad 1", Drain(scene));
        }
    }

    [Fact]
    public void TeleportPlayer_BadTarget_Throws_AndLeavesThePlayerWhereItWas()
    {
        var (assets, renderer, scene) = LoadSample();
        using (assets) using (renderer) using (scene)
        {
            Drain(scene);
            EngineException outside = Assert.Throws<EngineException>(() => scene.TeleportPlayer(100, 100));
            Assert.Contains("outside every room", outside.Message);
            EngineException notFinite = Assert.Throws<EngineException>(() => scene.TeleportPlayer(float.NaN, 0));
            Assert.Contains("finite", notFinite.Message);
            Assert.Equal(new Vec2(-4, 0), scene.Player!.Value.Position);
            Assert.Equal("Hall", scene.ActiveRoomName);
            Assert.Equal("", Drain(scene));
        }
    }

    [Fact]
    public void IsInsideTrigger_FollowsThePlayer_AndRejectsUnknownNames()
    {
        var (assets, renderer, scene) = LoadSample();
        using (assets) using (renderer) using (scene)
        {
            Assert.True(scene.IsInsideTrigger("StartPad"));
            Assert.False(scene.IsInsideTrigger("Door"));
            scene.TeleportPlayer(4, 0);
            Assert.False(scene.IsInsideTrigger("StartPad"));
            Assert.True(scene.IsInsideTrigger("Door"));
            EngineException ex = Assert.Throws<EngineException>(() => scene.IsInsideTrigger("Nope"));   // 밖(false)과 구별되는 실패
            Assert.Contains("no trigger named 'Nope'", ex.Message);
        }
    }

    [Fact]
    public void NodePosition_CanBeReadAndMoved_ButNotForThePlayerOrUnknownNodes()
    {
        var (assets, renderer, scene) = LoadSample();
        using (assets) using (renderer) using (scene)
        {
            Assert.Equal(new Vec3(-2, 0.5f, 2), scene.GetNodePosition("Pillar"));
            Assert.Null(scene.GetNodePosition("Nobody"));

            scene.SetNodePosition("Pillar", new Vec3(3, 1, -1));
            Assert.Equal(new Vec3(3, 1, -1), scene.GetNodePosition("Pillar"));

            EngineException player = Assert.Throws<EngineException>(() => scene.SetNodePosition("Hero", new Vec3(0, 0, 0)));
            Assert.Contains("is the player", player.Message);
            Assert.Contains("no node named 'Nobody'", Assert.Throws<EngineException>(() => scene.SetNodePosition("Nobody", new Vec3(0, 0, 0))).Message);
            Assert.Contains("finite", Assert.Throws<EngineException>(() => scene.SetNodePosition("Pillar", new Vec3(float.NaN, 0, 0))).Message);
            Assert.Equal(new Vec3(3, 1, -1), scene.GetNodePosition("Pillar"));   // 실패는 아무것도 바꾸지 않는다
        }
    }

    [Fact]
    public void WriteApis_AfterDispose_ThrowObjectDisposed()
    {
        var (assets, renderer, scene) = LoadSample();
        using (assets) using (renderer)
        {
            scene.Dispose();
            Assert.Throws<ObjectDisposedException>(() => scene.SetTriggerEnabled("StartPad", false));
            Assert.Throws<ObjectDisposedException>(() => scene.TeleportPlayer(0, 0));
            Assert.Throws<ObjectDisposedException>(() => scene.IsInsideTrigger("StartPad"));
            Assert.Throws<ObjectDisposedException>(() => scene.GetNodePosition("Pillar"));
            Assert.Throws<ObjectDisposedException>(() => scene.SetNodePosition("Pillar", new Vec3(0, 0, 0)));
        }
    }
}
