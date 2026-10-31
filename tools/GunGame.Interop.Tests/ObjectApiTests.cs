using GunGame.Interop;
using GunGame.Scene;
using GunGame.Scripting;
using GunGame.ScriptHost;

namespace GunGame.Interop.Tests;

/// <summary>
/// 오브젝트 쓰기 API 2단계: 표시 토글(렌더러 플래그), 회전/크기. C# 래퍼의 오류 변환, 스크립트 컨텍스트의 규칙(없는 노드는 false/null,
/// 비유한 값/플레이어 노드는 예외), 그리고 열쇠로 잠긴 문 샘플(LockedDoor)의 실제 동작. 엔진 규칙 자체는 engine_tests.cpp가 검증한다.
/// </summary>
public class ObjectApiTests
{
    private const float FrameSeconds = 1f / 30f;

    private sealed class Rig : IDisposable
    {
        private readonly TestAssets _assets = new();
        public EngineRenderer Renderer { get; } = new(96, 72, useWarp: true);
        public EngineScene Scene { get; }
        public StringWriter Log { get; } = new();

        public Rig(SceneDocument doc)
        {
            string path = _assets.Path_("scene.ggs");
            SceneBaker.Bake(doc, path);
            Scene = EngineScene.Load(Renderer, path);
        }

        public ScriptRunner Runner(params IGameScript[] scripts) => ScriptRunner.Create(Renderer, Scene, scripts, Log);

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

    private sealed class Lambda(Action<IScriptContext>? start = null, Action<IScriptContext, GameAction>? action = null) : IGameScript
    {
        public void OnStart(IScriptContext context) => start?.Invoke(context);
        public void OnAction(IScriptContext context, GameAction a) => action?.Invoke(context, a);
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

    /// <summary>플레이 샘플에 부모(Holder)와 자식(Item) 노드를 더한다: 부모를 숨기면 자식도 숨겨지는지 보려는 용도.</summary>
    private static SceneDocument DocumentWithHierarchy()
    {
        SceneDocument doc = PlaySamples.Load();
        string cube = Path.Combine(SamplesDir(), "cube.obj");
        var holder = new SceneNode { Name = "Holder", Position = new Vec3(-3, 1, 1) };
        doc.Add(holder);
        doc.Add(new SceneNode { Name = "Item", MeshPath = cube, Position = new Vec3(0.5f, 0, 0), Scale = new Vec3(0.3f, 0.3f, 0.3f) }, holder);
        return doc;
    }

    // ---- 렌더러 래퍼 ----
    [Fact]
    public void EngineRenderer_ObjectVisible_HidesAndRestores_AndRejectsUnknownIds()
    {
        using var r = new EngineRenderer(64, 48, useWarp: true);
        r.SetCamera(new Vec3(0, 2, 6), new Vec3(0, 0.5f, 0), 0.8f);
        int obj = r.CreateObject(r.LoadMeshObj(Path.Combine(SamplesDir(), "cube.obj")));
        r.Render();
        byte[] shown = r.Pixels.ToArray();

        Assert.True(r.IsObjectVisible(obj));                  // 처음엔 보임
        r.SetObjectVisible(obj, false);
        Assert.False(r.IsObjectVisible(obj));
        r.Render();
        Assert.NotEqual(shown, r.Pixels.ToArray());
        r.SetObjectVisible(obj, true);
        r.Render();
        Assert.Equal(shown, r.Pixels.ToArray());

        Assert.Contains("unknown object id 9999", Assert.Throws<EngineException>(() => r.SetObjectVisible(9999, false)).Message);
        Assert.Contains("unknown object id 9999", Assert.Throws<EngineException>(() => r.IsObjectVisible(9999)).Message);
    }

    [Fact]
    public void EngineRenderer_SetObjectTransform_RejectsNonFiniteValues_AndKeepsTheOldTransform()
    {
        using var r = new EngineRenderer(64, 48, useWarp: true);
        r.SetCamera(new Vec3(0, 2, 6), new Vec3(0, 0.5f, 0), 0.8f);
        int obj = r.CreateObject(r.LoadMeshObj(Path.Combine(SamplesDir(), "cube.obj")));
        r.Render();
        byte[] before = r.Pixels.ToArray();
        var ok = new Vec3(0, 0, 0);
        var one = new Vec3(1, 1, 1);
        Assert.Contains("must be finite", Assert.Throws<EngineException>(() => r.SetObjectTransform(obj, new Vec3(0, float.NaN, 0), ok, one)).Message);
        Assert.Throws<EngineException>(() => r.SetObjectTransform(obj, ok, new Vec3(0, 0, float.PositiveInfinity), one));
        Assert.Throws<EngineException>(() => r.SetObjectTransform(obj, ok, ok, new Vec3(float.NegativeInfinity, 1, 1)));
        r.Render();
        Assert.Equal(before, r.Pixels.ToArray());
    }

    // ---- 씬 래퍼 ----
    [Fact]
    public void EngineScene_RotationScaleVisible_ReadWrite_AndFollowTheErrorRules()
    {
        using var rig = new Rig(PlaySamples.Load());
        EngineScene scene = rig.Scene;
        Assert.Equal(new Vec3(0, 0, 0), scene.GetNodeRotation("Pillar"));
        Assert.Equal(new Vec3(0.5f, 0.5f, 0.5f), scene.GetNodeScale("Pillar"));
        Assert.True(scene.IsNodeVisible("Pillar"));
        Assert.Null(scene.GetNodeRotation("Nope"));
        Assert.Null(scene.GetNodeScale("Nope"));
        Assert.Null(scene.IsNodeVisible("Nope"));

        scene.SetNodeRotation("Pillar", new Vec3(0, 0.7f, 0.2f));
        scene.SetNodeScale("Pillar", new Vec3(1, 2, 3));
        scene.SetNodeVisible("Pillar", false);
        Assert.Equal(new Vec3(0, 0.7f, 0.2f), scene.GetNodeRotation("Pillar"));
        Assert.Equal(new Vec3(1, 2, 3), scene.GetNodeScale("Pillar"));
        Assert.False(scene.IsNodeVisible("Pillar"));
        Assert.Equal(new Vec3(-2, 0.5f, 2), scene.GetNodePosition("Pillar"));   // 다른 값은 그대로

        scene.SetNodeScale("Pillar", new Vec3(0, 0, 0));                         // 크기 0은 받는다
        Assert.Equal(new Vec3(0, 0, 0), scene.GetNodeScale("Pillar"));

        Assert.Contains("no node named 'Nope'", Assert.Throws<EngineException>(() => scene.SetNodeRotation("Nope", new Vec3(0, 0, 0))).Message);
        Assert.Contains("no node named 'Nope'", Assert.Throws<EngineException>(() => scene.SetNodeScale("Nope", new Vec3(1, 1, 1))).Message);
        Assert.Contains("no node named 'Nope'", Assert.Throws<EngineException>(() => scene.SetNodeVisible("Nope", false)).Message);
        Assert.Contains("rotation must be finite", Assert.Throws<EngineException>(() => scene.SetNodeRotation("Pillar", new Vec3(float.NaN, 0, 0))).Message);
        Assert.Contains("scale must be finite", Assert.Throws<EngineException>(() => scene.SetNodeScale("Pillar", new Vec3(1, float.PositiveInfinity, 1))).Message);
        Assert.Contains("is the player", Assert.Throws<EngineException>(() => scene.SetNodeRotation("Hero", new Vec3(0, 1, 0))).Message);
        Assert.Contains("is the player", Assert.Throws<EngineException>(() => scene.SetNodeScale("Hero", new Vec3(2, 2, 2))).Message);
        Assert.Equal(new Vec3(0, 0.7f, 0.2f), scene.GetNodeRotation("Pillar"));   // 실패는 아무것도 바꾸지 않는다

        scene.SetNodeVisible("Hero", false);                                      // 플레이어는 숨길 수 있다
        Assert.False(scene.IsNodeVisible("Hero"));
        scene.Step(1, 0, FrameSeconds);
        Assert.True(scene.Player!.Value.Position.X > -4f);                        // 숨긴 채로도 걷는다
    }

    // ---- 스크립트 컨텍스트 ----
    [Fact]
    public void Context_Visible_HidesTheSubtree_ButReadsOnlyTheNodesOwnFlag()
    {
        using var rig = new Rig(DocumentWithHierarchy());
        byte[] shown = rig.Frame();
        bool? holderFlag = null, itemFlag = null;
        bool setUnknown = true;
        bool? unknown = false;
        rig.Runner(new Lambda(start: c =>
        {
            Assert.True(c.SetObjectVisible("Holder", false));
            holderFlag = c.IsObjectVisible("Holder");
            itemFlag = c.IsObjectVisible("Item");        // 부모가 숨겨졌어도 자식 자신의 플래그는 true
            setUnknown = c.SetObjectVisible("Nope", false);
            unknown = c.IsObjectVisible("Nope");
        })).Start();
        Assert.Equal(false, holderFlag);
        Assert.Equal(true, itemFlag);
        Assert.False(setUnknown);
        Assert.Null(unknown);
        byte[] holderHidden = rig.Frame();
        Assert.NotEqual(shown, holderHidden);          // 자식(Item)까지 사라져 화면이 바뀐다

        rig.Runner(new Lambda(start: c => c.SetObjectVisible("Holder", true))).Start();
        Assert.Equal(shown, rig.Frame());              // 다시 켜면 그대로 돌아온다
    }

    [Fact]
    public void Context_RotationAndScale_FollowThePositionRules()
    {
        using var rig = new Rig(PlaySamples.Load());
        (float X, float Y, float Z)? read = null, unknownRotation = (9, 9, 9), unknownScale = (9, 9, 9);
        bool setRotation = false, setScale = false, setUnknownRotation = true, setUnknownScale = true;
        (float X, float Y, float Z)? heroRotationRead = null;
        rig.Runner(new Lambda(start: c =>
        {
            setRotation = c.SetObjectRotation("Pillar", 0, 1.2f, 0);
            read = c.GetObjectRotation("Pillar");
            setScale = c.SetObjectScale("Pillar", 1, 1, 1);
            unknownRotation = c.GetObjectRotation("Nope");
            unknownScale = c.GetObjectScale("Nope");
            setUnknownRotation = c.SetObjectRotation("Nope", 0, 0, 0);
            setUnknownScale = c.SetObjectScale("Nope", 1, 1, 1);
            heroRotationRead = c.GetObjectRotation("Hero");     // 읽기는 플레이어 노드도 된다
        })).Start();
        Assert.True(setRotation && setScale);
        Assert.Equal((0f, 1.2f, 0f), read);
        Assert.Null(unknownRotation);
        Assert.Null(unknownScale);
        Assert.False(setUnknownRotation);
        Assert.False(setUnknownScale);
        Assert.NotNull(heroRotationRead);

        // 플레이어 노드와 비유한 값은 예외(스크립트 오류로 보고된다).
        Assert.Contains("is the player", Assert.Throws<ScriptException>(rig.Runner(new Lambda(start: c => c.SetObjectRotation("Hero", 0, 1, 0))).Start).Message);
        Assert.Contains("is the player", Assert.Throws<ScriptException>(rig.Runner(new Lambda(start: c => c.SetObjectScale("Hero", 2, 2, 2))).Start).Message);
        Assert.Contains("rotation must be finite", Assert.Throws<ScriptException>(rig.Runner(new Lambda(start: c => c.SetObjectRotation("Pillar", float.NaN, 0, 0))).Start).Message);
        Assert.Contains("scale must be finite", Assert.Throws<ScriptException>(rig.Runner(new Lambda(start: c => c.SetObjectScale("Pillar", 1, float.NegativeInfinity, 1))).Start).Message);
    }

    // ---- 열쇠로 잠긴 문 샘플(LockedDoor 스크립트, 실제 동작) ----
    [Fact]
    public void LockedDoorSample_KeyHidesAndDoorSlidesDownAndOpens_OnTheRealScene()
    {
        SceneDocument doc = SceneSerializer.Load(Path.Combine(SamplesDir(), "locked_door.scene.json"));
        using var rig = new Rig(doc);
        ScriptRunner runner = ScriptRunner.Create(rig.Renderer, rig.Scene, SampleScriptsDll(), "LockedDoor", rig.Log);
        runner.Start();
        byte[] initial = rig.Frame();
        Assert.True(rig.Scene.IsNodeVisible("Key"));
        Assert.Equal(new Vec3(-0.85f, 1, 0), rig.Scene.GetNodePosition("DoorPanel"));

        rig.Scene.TeleportPlayer(-1.8f, 0);                      // DoorPad 안, 열쇠 없이
        runner.HandleAction(GameAction.Interact);
        Assert.Contains("the door is locked", rig.Log.ToString());
        Assert.Equal(new Vec3(-0.85f, 1, 0), rig.Scene.GetNodePosition("DoorPanel"));   // 잠겨 있으면 문이 그대로다

        rig.Scene.TeleportPlayer(-4, 2);                         // KeyPad 안
        runner.HandleAction(GameAction.Interact);
        Assert.Equal(false, rig.Scene.IsNodeVisible("Key"));     // 열쇠를 주워 숨겼다(알파 0이 아니라 숨김)
        Assert.Contains("picked up the key", rig.Log.ToString());

        rig.Scene.TeleportPlayer(-1.8f, 0);                      // DoorPad 안, 열쇠 있음
        runner.HandleAction(GameAction.Interact);
        Assert.Contains("the door opens", rig.Log.ToString());
        Vec3 panel = rig.Scene.GetNodePosition("DoorPanel")!.Value;
        Assert.Equal(-0.85f, panel.X);
        Assert.Equal(1f - 2.2f, panel.Y, 4);                     // 문판이 바닥 아래로 내려갔다
        Assert.NotEqual(initial, rig.Frame());

        for (int i = 0; i < 40; i++)
            rig.Scene.Step(1, 0, FrameSeconds);                  // 열린 문을 지나 Annex로
        Assert.Equal("Annex", rig.Scene.ActiveRoomName);
    }
}
