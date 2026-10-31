using GunGame.Interop;
using GunGame.Scene;
using GunGame.Scripting;
using GunGame.ScriptHost;

namespace GunGame.Interop.Tests;

/// <summary>
/// 오브젝트 생성/삭제(P2 4단계)의 C# 래퍼와 스크립트 컨텍스트 규칙: 없는 템플릿/인스턴스는 false, 이름 충돌/한도/씬 노드 삭제 같은 실제 오류는 엔진 사유가 담긴 예외.
/// 엔진 자체의 규칙(구운 값 시작, 원자성, 한도, 불변식)은 engine_tests.cpp가 검증한다.
/// </summary>
public class SpawnApiTests
{
    private sealed class Rig : IDisposable
    {
        private readonly TestAssets _assets = new();
        public EngineRenderer Renderer { get; } = new(96, 72, useWarp: true);
        public EngineScene Scene { get; }
        public StringWriter Log { get; } = new();

        public Rig()
        {
            string path = _assets.Path_("scene.ggs");
            SceneBaker.Bake(PlaySamples.Load(), path);
            Scene = EngineScene.Load(Renderer, path);
        }

        public ScriptRunner Runner(params IGameScript[] scripts) => ScriptRunner.Create(Renderer, Scene, scripts, Log);

        public void Dispose()
        {
            Scene.Dispose();   // 씬은 렌더러보다 먼저
            Renderer.Dispose();
            _assets.Dispose();
        }
    }

    private sealed class Lambda(Action<IScriptContext> start) : IGameScript
    {
        public void OnStart(IScriptContext context) => start(context);
    }

    [Fact]
    public void EngineScene_SpawnAndDestroy_FollowTheRulesAndReportErrors()
    {
        using var rig = new Rig();
        EngineScene scene = rig.Scene;
        Assert.True(scene.ObjectExists("Pillar"));
        Assert.False(scene.ObjectExists("P1"));
        Assert.Equal(0, scene.DynamicNodeCount);

        Vec3 baked = scene.GetNodePosition("Pillar") ?? throw new InvalidOperationException("no Pillar");
        scene.SetNodePosition("Pillar", new Vec3(9, 9, 9));   // 템플릿을 옮겨도 복제는 구운 위치가 아니라 인자 위치에서 시작한다
        scene.SpawnObject("Pillar", "P1", new Vec3(1, 0.5f, -2));
        Assert.True(scene.ObjectExists("P1"));
        Assert.Equal(1, scene.DynamicNodeCount);
        Assert.Equal(new Vec3(1, 0.5f, -2), scene.GetNodePosition("P1"));
        Assert.Equal(new Vec3(9, 9, 9), scene.GetNodePosition("Pillar"));
        scene.SetNodePosition("Pillar", baked);

        // 같은 이름/빈 이름/'/'/씬 노드와 같은 이름/없는 템플릿/플레이어/복제본 복제/비유한 값은 사유가 담긴 예외이고 아무것도 늘지 않는다.
        EngineException e = Assert.Throws<EngineException>(() => scene.SpawnObject("Pillar", "P1", default));
        Assert.Contains("already exists", e.Message);
        Assert.Contains("must not be empty", Assert.Throws<EngineException>(() => scene.SpawnObject("Pillar", "", default)).Message);
        Assert.Contains("must not contain '/'", Assert.Throws<EngineException>(() => scene.SpawnObject("Pillar", "a/b", default)).Message);
        Assert.Contains("already exists", Assert.Throws<EngineException>(() => scene.SpawnObject("Pillar", "Hero", default)).Message);
        Assert.Contains("no node named 'Nope'", Assert.Throws<EngineException>(() => scene.SpawnObject("Nope", "N1", default)).Message);
        Assert.Contains("is or contains the player", Assert.Throws<EngineException>(() => scene.SpawnObject("Hero", "N1", default)).Message);
        Assert.Contains("is a spawned object", Assert.Throws<EngineException>(() => scene.SpawnObject("P1", "N1", default)).Message);
        Assert.Contains("must be finite", Assert.Throws<EngineException>(() => scene.SpawnObject("Pillar", "N1", new Vec3(float.NaN, 0, 0))).Message);
        Assert.Equal(1, scene.DynamicNodeCount);
        Assert.False(scene.ObjectExists("N1"));

        Assert.Contains("was not created by spawn_object", Assert.Throws<EngineException>(() => scene.DestroyObject("Pillar")).Message);
        Assert.Contains("no node named 'Nope'", Assert.Throws<EngineException>(() => scene.DestroyObject("Nope")).Message);
        scene.DestroyObject("P1");
        Assert.False(scene.ObjectExists("P1"));
        Assert.Equal(0, scene.DynamicNodeCount);
        Assert.Null(scene.GetNodePosition("P1"));   // 지운 이름은 없는 노드 규칙
        scene.SpawnObject("Pillar", "P1", default); // 이름 재사용
        Assert.True(scene.ObjectExists("P1"));
    }

    [Fact]
    public void Context_SpawnApi_ReturnsFalseForUnknownNames_AndThrowsForRealErrors()
    {
        using var rig = new Rig();
        var lines = new List<string>();
        rig.Runner(new Lambda(c =>
        {
            lines.Add("unknown template: " + c.SpawnObject("Nope", "N1", 0, 0, 0));
            lines.Add("spawn: " + c.SpawnObject("Pillar", "P1", 1, 0.5f, -2));
            lines.Add("exists: " + c.ObjectExists("P1") + " " + c.ObjectExists("P2") + " " + c.ObjectExists("Pillar"));
            lines.Add("position: " + c.GetObjectPosition("P1"));   // 기존 이름 API가 복제본에 그대로 동작한다
            lines.Add("move: " + c.SetObjectPosition("P1", 2, 0.5f, -2) + " " + c.GetObjectPosition("P1"));
            lines.Add("hide: " + c.SetObjectVisible("P1", false) + " " + c.IsObjectVisible("P1"));
            lines.Add("destroy unknown: " + c.DestroyObject("Nope"));
            lines.Add("destroy: " + c.DestroyObject("P1") + " " + c.ObjectExists("P1"));
            lines.Add("after destroy: " + c.SetObjectPosition("P1", 0, 0, 0) + " " + c.GetObjectPosition("P1") + " " + c.IsObjectVisible("P1"));
            Assert.Throws<EngineException>(() => c.DestroyObject("Pillar"));                            // 씬 노드는 못 지운다
            Assert.Throws<EngineException>(() => c.SpawnObject("Pillar", "Hero", 0, 0, 0));             // 이름 충돌
            Assert.Throws<EngineException>(() => c.SpawnObject("Pillar", "P2", float.PositiveInfinity, 0, 0));
        })).Start();
        Assert.Equal(
        [
            "unknown template: False",
            "spawn: True",
            "exists: True False True",
            "position: (1, 0.5, -2)",
            "move: True (2, 0.5, -2)",
            "hide: True False",
            "destroy unknown: False",
            "destroy: True False",
            "after destroy: False  ",
        ], lines);
        Assert.Equal(0, rig.Scene.DynamicNodeCount);
    }
}
