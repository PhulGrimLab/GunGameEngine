using GunGame.Interop;
using GunGame.Scene;
using GunGame.Scripting;
using GunGame.ScriptHost;

namespace GunGame.Interop.Tests;

// GunGame.Interop에도 TriggerEvent가 있어, 이 네임스페이스 안에서 스크립팅 쪽 것을 쓰도록 별칭을 네임스페이스 안에 둔다.
using TriggerEvent = GunGame.Scripting.TriggerEvent;

/// <summary>순서 확인용 공개 스크립트(타입 이름 순서로 만들어져야 한다). 일부러 Zeta를 먼저 선언했다.</summary>
public sealed class ZetaOrderScript : IGameScript;

/// <inheritdoc cref="ZetaOrderScript"/>
public sealed class AlphaOrderScript : IGameScript;

/// <summary>
/// 스크립트 실행기(ScriptRunner)를 실제 엔진 씬 위에서 검증한다: 호출 순서, 오류 포장, 엔진 쓰기 API 연결(텔레포트/트리거/알파),
/// 어셈블리 불러오기 규칙, 빌려 쓰는 핸들(Attach). hostfxr 호스팅 자체와 플레이어 연동은 ctest의 gge_player_script_*가 검증한다.
/// </summary>
public class ScriptRunnerTests
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

        public string Drain()
        {
            var events = new List<string>();
            while (Scene.TryPollEvent(out SceneEventData e))
                events.Add($"{e.Kind} {e.TriggerName} {e.Tag}");
            return string.Join(", ", events);
        }

        public byte[] Frame()
        {
            Renderer.Render();
            return Renderer.Pixels.ToArray();
        }

        public void Dispose()
        {
            Scene.Dispose();   // 씬은 렌더러보다 먼저
            Renderer.Dispose();
            _assets.Dispose();
        }
    }

    private sealed class Recorder(string name, List<string> calls) : IGameScript
    {
        public void OnStart(IScriptContext context) => calls.Add($"{name}.start room={context.ActiveRoomName}");
        public void OnTrigger(IScriptContext context, TriggerEvent trigger) => calls.Add($"{name}.{trigger.Kind} {trigger.Name} {trigger.Tag}");
        public void OnUpdate(IScriptContext context, float deltaSeconds) => calls.Add($"{name}.update {deltaSeconds:0.000}");
    }

    private sealed class Lambda(Action<IScriptContext>? start = null, Action<IScriptContext, TriggerEvent>? trigger = null) : IGameScript
    {
        public void OnStart(IScriptContext context) => start?.Invoke(context);
        public void OnTrigger(IScriptContext context, TriggerEvent e) => trigger?.Invoke(context, e);
    }

    private static string SampleScriptsDll()
    {
        for (string? dir = AppContext.BaseDirectory; dir is not null; dir = Path.GetDirectoryName(dir))
        {
            string candidate = Path.Combine(dir, "build", "bin", "scripts", "GunGame.SampleScripts.dll");
            if (File.Exists(candidate))
                return candidate;
        }
        throw new FileNotFoundException("build/bin/scripts/GunGame.SampleScripts.dll not found (run the tools build first)");
    }

    // ---- 호출 순서 ----
    [Fact]
    public void Scripts_RunInOrder_StartFirst_ThenEachEventAndUpdate_ToEveryScript()
    {
        using var rig = new Rig();
        var calls = new List<string>();
        ScriptRunner runner = rig.Runner(new Recorder("A", calls), new Recorder("B", calls));
        runner.Start();
        runner.HandleTrigger(new TriggerEvent(TriggerKind.Enter, 1, "StartPad"));
        runner.Update(1f / 30f);
        Assert.Equal(["A.start room=Hall", "B.start room=Hall", "A.Enter StartPad 1", "B.Enter StartPad 1", "A.update 0.033", "B.update 0.033"], calls);
    }

    [Fact]
    public void EventsAndUpdatesBeforeStart_AreRejected_AndStartRunsOnlyOnce()
    {
        using var rig = new Rig();
        ScriptRunner runner = rig.Runner(new Recorder("A", []));
        Assert.Throws<InvalidOperationException>(() => runner.HandleTrigger(new TriggerEvent(TriggerKind.Enter, 1, "x")));
        Assert.Throws<InvalidOperationException>(() => runner.Update(0.1f));
        runner.Start();
        Assert.Throws<InvalidOperationException>(runner.Start);
    }

    // ---- 오류 포장 ----
    [Fact]
    public void ScriptExceptions_AreWrappedWithTheScriptTheCallAndTheCause()
    {
        using var rig = new Rig();
        ScriptRunner runner = rig.Runner(new Lambda(trigger: (_, _) => throw new InvalidOperationException("boom")));
        runner.Start();
        ScriptException ex = Assert.Throws<ScriptException>(() => runner.HandleTrigger(new TriggerEvent(TriggerKind.Exit, 7, "Door")));
        Assert.Contains("Lambda", ex.Message);
        Assert.Contains("OnTrigger", ex.Message);
        Assert.Contains("InvalidOperationException: boom", ex.Message);
        Assert.IsType<InvalidOperationException>(ex.InnerException);
    }

    [Fact]
    public void EngineErrorsInsideAScript_SurfaceAsScriptErrorsWithTheEngineReason()
    {
        using var rig = new Rig();
        ScriptRunner runner = rig.Runner(new Lambda(start: c => c.SetTriggerEnabled("NoSuchTrigger", false)));
        ScriptException ex = Assert.Throws<ScriptException>(runner.Start);
        Assert.Contains("OnStart", ex.Message);
        Assert.Contains("no trigger named 'NoSuchTrigger'", ex.Message);
    }

    // ---- 엔진 쓰기 API 연결 ----
    [Fact]
    public void Context_TeleportsAndTogglesTriggers_OnTheRealScene()
    {
        using var rig = new Rig();
        rig.Drain();
        ScriptRunner runner = rig.Runner(new Lambda(
            start: c => c.SetTriggerEnabled("Door", false),
            trigger: (c, e) => { if (e.Kind == TriggerKind.Enter && e.Tag == 1) c.TeleportPlayer(4, 0); }));
        runner.Start();
        runner.HandleTrigger(new TriggerEvent(TriggerKind.Enter, 1, "StartPad"));
        Assert.Equal(new Vec2(4, 0), rig.Scene.Player!.Value.Position);
        Assert.Equal("Annex", rig.Scene.ActiveRoomName);
        Assert.Equal("Exit StartPad 1", rig.Drain());   // Door가 꺼져 있어 Enter Door가 없다
    }

    [Fact]
    public void Context_ReportsRoomAndPlayerPosition()
    {
        using var rig = new Rig();
        (string Room, (float X, float Z)? Position) seen = default;
        rig.Runner(new Lambda(start: c => seen = (c.ActiveRoomName, c.PlayerPosition))).Start();
        Assert.Equal("Hall", seen.Room);
        Assert.Equal((-4f, 0f), seen.Position);
    }

    [Fact]
    public void Context_SetObjectAlpha_ChangesTheFrame_AndReportsUnknownNodes()
    {
        using var rig = new Rig();
        // 기둥이 화면에 보이도록 Annex 쪽으로 옮겨 놓고 비교한다.
        rig.Scene.TeleportPlayer(4, 0);
        byte[] opaque = rig.Frame();
        bool found = false, missing = true;
        rig.Runner(new Lambda(start: c =>
        {
            found = c.SetObjectAlpha("Pillar", 0f);
            missing = c.SetObjectAlpha("NoSuchNode", 0.5f);
        })).Start();
        Assert.True(found);
        Assert.False(missing);
        Assert.NotEqual(opaque, rig.Frame());
    }

    [Fact]
    public void Context_Log_WritesTaggedLinesToTheGivenWriter()
    {
        using var rig = new Rig();
        rig.Runner(new Lambda(start: c => c.Log("hello"))).Start();
        Assert.Equal("[script] hello" + Environment.NewLine, rig.Log.ToString());
    }

    // ---- 어셈블리 불러오기 ----
    [Fact]
    public void LoadScripts_WithoutAFilter_CreatesEveryPublicScript_InTypeNameOrder()
    {
        // 이 테스트 어셈블리의 공개 스크립트(아래 두 개)만 대상이다. 중첩/비공개 클래스(Recorder, Lambda)는 제외된다.
        IReadOnlyList<IGameScript> scripts = ScriptRunner.LoadScripts(typeof(ScriptRunnerTests).Assembly.Location, null);
        Assert.Equal(["AlphaOrderScript", "ZetaOrderScript"], scripts.Select(s => s.GetType().Name));
    }

    [Fact]
    public void LoadScripts_TypeFilter_MatchesShortOrFullName_AndRejectsUnknownWithTheAvailableList()
    {
        string dll = SampleScriptsDll();
        Assert.Equal("ReturnToHall", Assert.Single(ScriptRunner.LoadScripts(dll, "ReturnToHall")).GetType().Name);
        Assert.Equal("ReturnToHall", Assert.Single(ScriptRunner.LoadScripts(dll, "GunGame.SampleScripts.ReturnToHall")).GetType().Name);

        ScriptException ex = Assert.Throws<ScriptException>(() => ScriptRunner.LoadScripts(dll, "Nope"));
        Assert.Contains("no script type 'Nope'", ex.Message);
        Assert.Contains("GunGame.SampleScripts.DoorLock", ex.Message);
        Assert.Contains("GunGame.SampleScripts.ReturnToHall", ex.Message);
    }

    [Fact]
    public void LoadScripts_BrokenConstructor_ReportsTheCause()
    {
        ScriptException ex = Assert.Throws<ScriptException>(() => ScriptRunner.LoadScripts(SampleScriptsDll(), "BrokenConstructor"));
        Assert.Contains("cannot create script GunGame.SampleScripts.BrokenConstructor: cannot be constructed", ex.Message);
    }

    [Fact]
    public void LoadScripts_RejectsMissingFiles_NonAssemblies_AndAssembliesWithoutScripts()
    {
        using var assets = new TestAssets();
        Assert.Contains("not found", Assert.Throws<ScriptException>(() => ScriptRunner.LoadScripts(assets.Path_("nope.dll"), null)).Message);

        string notAnAssembly = assets.Path_("text.dll");
        File.WriteAllText(notAnAssembly, "this is not a .NET assembly");
        Assert.Contains("cannot load script assembly", Assert.Throws<ScriptException>(() => ScriptRunner.LoadScripts(notAnAssembly, null)).Message);

        string withoutScripts = typeof(EngineScene).Assembly.Location;   // 공개 IGameScript 구현이 없는 어셈블리
        Assert.Contains("no public IGameScript implementation", Assert.Throws<ScriptException>(() => ScriptRunner.LoadScripts(withoutScripts, null)).Message);
    }

    [Fact]
    public void LoadedSampleScripts_RunOnTheRealScene_EndToEnd()
    {
        using var rig = new Rig();
        rig.Drain();
        ScriptRunner runner = ScriptRunner.Create(rig.Renderer, rig.Scene, SampleScriptsDll(), "ReturnToHall", rig.Log);
        runner.Start();
        rig.Scene.TeleportPlayer(4, 0);   // Door 안
        Assert.Equal("Exit StartPad 1, Enter Door 7", rig.Drain());
        runner.HandleTrigger(new TriggerEvent(TriggerKind.Enter, 7, "Door"));
        Assert.Equal(new Vec2(-4, 0), rig.Scene.Player!.Value.Position);
        Assert.Equal("Exit Door 7, Enter StartPad 1", rig.Drain());
        Assert.Contains("[script] Door: sending the player back to the hall", rig.Log.ToString());
    }

    // ---- 빌려 쓰는 핸들 ----
    [Fact]
    public void AttachedHandles_AreBorrowed_DisposingThemLeavesTheOriginalsWorking()
    {
        using var rig = new Rig();
        EngineScene borrowedScene = EngineScene.Attach(rig.Scene.NativeHandle);
        EngineRenderer borrowedRenderer = EngineRenderer.Attach(rig.Renderer.NativeHandle, 96, 72);
        Assert.Equal((96, 72), (borrowedRenderer.Width, borrowedRenderer.Height));
        borrowedScene.TeleportPlayer(4, 0);            // 같은 네이티브 씬에 작용한다
        Assert.Equal("Annex", rig.Scene.ActiveRoomName);
        borrowedScene.Dispose();
        borrowedRenderer.Dispose();
        rig.Scene.Step(0, 0, 1f / 30f);                // 원본은 멀쩡하다
        rig.Renderer.Render();
        Assert.Throws<ObjectDisposedException>(() => borrowedScene.Step(0, 0, 0));

        Assert.Throws<ArgumentException>(() => EngineScene.Attach(0));
        Assert.Throws<ArgumentException>(() => EngineRenderer.Attach(0, 1, 1));
    }
}
