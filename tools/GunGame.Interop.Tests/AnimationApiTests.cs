using GunGame.Interop;
using GunGame.Scene;
using GunGame.Scripting;
using GunGame.ScriptHost;

namespace GunGame.Interop.Tests;

/// <summary>
/// 클립 재생 전환(P2 3단계): C++ Scene의 상태 기계가 C# AnimationPlayer와 같은 규칙인지 프레임마다 본 행렬로 교차 검증하고,
/// C# 래퍼/스크립트 컨텍스트(PlayClip/StopClip/SetClipSpeed/SetClipPlaying/GetClip/PlayerSpeed)의 규칙과 오류를 확인한다.
/// 엔진 자체의 규칙 표는 engine_tests.cpp가 검증한다.
/// </summary>
public class AnimationApiTests
{
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

    /// <summary>씬 파일의 트리: 스킨 노드 T(초기 클립 wave), 클립 없는 스킨 노드 R, 스킨이 아닌 노드들(큐브), 플레이어 Hero(큐브).</summary>
    private static SceneDocument AnimatedDocument()
    {
        SceneDocument doc = PlaySamples.Load();
        string glb = Path.Combine(SamplesDir(), "tentacle.glb");
        doc.Add(new SceneNode { Name = "T", MeshPath = glb, Position = new Vec3(-2.5f, 0, -1), AnimationClip = "wave" });
        doc.Add(new SceneNode { Name = "R", MeshPath = glb, Position = new Vec3(-1, 0, -2) });
        return doc;
    }

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

    // ---- 교차 검증: C++ Scene = C# AnimationPlayer ----
    private enum Op { Play, Stop, Speed, Pause, Step }

    private readonly record struct Command(Op Kind, string Clip = "", float Value = 0, bool Restart = false, bool Loop = true, int Count = 1);

    private static IEnumerable<Command> Script()
    {
        yield return new Command(Op.Step, Value: 0.1f, Count: 7);                                  // 초기 클립(wave)이 0.7초 진행
        yield return new Command(Op.Play, "curl", Value: 0.3f);                                    // 크로스페이드로 전환
        yield return new Command(Op.Step, Value: 0.1f, Count: 5);                                  // 페이드 중
        yield return new Command(Op.Step, Value: 0.1f, Count: 5);                                  // 페이드 끝
        yield return new Command(Op.Play, "curl", Value: 0.5f);                                    // 같은 클립: 무시
        yield return new Command(Op.Step, Value: 0.1f, Count: 3);
        yield return new Command(Op.Play, "curl", Restart: true, Loop: false);                     // 같은 클립 restart + 한 번 재생
        yield return new Command(Op.Step, Value: 0.1f, Count: 25);                                 // 2.5초: 끝(2초)에서 멈춤
        yield return new Command(Op.Play, "curl", Loop: true);                                     // 끝난 클립을 restart 없이: 무시(loop 값도 무시)
        yield return new Command(Op.Step, Value: 0.1f, Count: 3);
        yield return new Command(Op.Play, "curl", Restart: true, Loop: true);                      // 다시 반복으로
        yield return new Command(Op.Step, Value: 0.1f, Count: 22);                                 // 2초를 넘겨 감김
        yield return new Command(Op.Play, "wave", Value: 0.5f, Loop: false);                       // 반복 중인 클립에서 한 번 재생으로 크로스페이드
        yield return new Command(Op.Step, Value: 0.1f, Count: 3);                                  // 페이드 중
        yield return new Command(Op.Play, "curl", Value: 0.5f);                                    // 페이드 중 다시 전환: 지금 클립(wave)에서 새로 페이드
        yield return new Command(Op.Step, Value: 0.1f, Count: 10);
        yield return new Command(Op.Play, "wave", Value: 0.4f, Loop: false);                       // 한 번 재생 클립으로 페이드
        yield return new Command(Op.Step, Value: 0.1f, Count: 30);                                 // 끝까지 가서 멈춤
        yield return new Command(Op.Play, "curl", Value: 0.5f);                                    // 끝난 한 번 재생 클립에서 크로스페이드(끝 포즈 유지)
        yield return new Command(Op.Step, Value: 0.1f, Count: 4);
        yield return new Command(Op.Pause, Value: 0);                                              // 일시정지(페이드 진행도 멈춤)
        yield return new Command(Op.Step, Value: 0.1f, Count: 5);
        yield return new Command(Op.Play, "wave", Restart: true);                                  // 일시정지 중 전환: 포즈 즉시 반영
        yield return new Command(Op.Step, Value: 0.1f, Count: 2);
        yield return new Command(Op.Pause, Value: 1);
        yield return new Command(Op.Step, Value: 0.1f, Count: 4);
        yield return new Command(Op.Speed, Value: -1.5f);                                          // 거꾸로
        yield return new Command(Op.Step, Value: 0.1f, Count: 6);
        yield return new Command(Op.Speed, Value: 2f);
        yield return new Command(Op.Step, Value: 0.1f, Count: 3);
        yield return new Command(Op.Speed, Value: 0f);
        yield return new Command(Op.Step, Value: 0.1f, Count: 2);
        yield return new Command(Op.Speed, Value: 1f);
        yield return new Command(Op.Play, "curl", Restart: true, Loop: false);
        yield return new Command(Op.Speed, Value: -1f);                                            // 한 번 재생 + 음수 속도: 0초에서 이미 끝난 상태
        yield return new Command(Op.Step, Value: 0.1f, Count: 3);
        yield return new Command(Op.Speed, Value: 1f);
        yield return new Command(Op.Step, Value: 0.1f, Count: 30);                                 // 속도를 되돌리면 정방향으로 끝까지
        yield return new Command(Op.Stop);                                                         // 레스트 포즈
        yield return new Command(Op.Step, Value: 0.1f, Count: 3);
        yield return new Command(Op.Play, "wave", Value: 0.5f);                                    // 레스트에서 시작: 페이드 없이 즉시
        yield return new Command(Op.Step, Value: 0.1f, Count: 3);
    }

    [Fact]
    public void Scene_AndAnimationPlayer_ProduceTheSameBoneMatricesOnEveryFrame_ThroughAClipSwitchingScript()
    {
        using var rig = new Rig(AnimatedDocument());
        ImportedModel model = GltfImporter.Import(Path.Combine(SamplesDir(), "tentacle.glb"));
        int bones = model.Skeleton!.BoneCount;

        // C# 쪽: 같은 모델을 다른 렌더러에 올리고 AnimationPlayer가 시간/페이드를 관리한다.
        using var reference = new EngineRenderer(64, 48, useWarp: true);
        int mesh = reference.CreateSkinnedMesh(model.Vertices, model.Joints!, model.Weights!, model.Indices);
        int skeleton = reference.CreateSkeleton(model.Skeleton);
        int obj = reference.CreateObject(mesh);
        reference.SetObjectSkeleton(obj, skeleton);
        var refs = new Dictionary<string, ClipRef>();
        var names = new Dictionary<int, string>();
        foreach (ClipData clip in model.Clips)
        {
            var r = new ClipRef(reference.CreateClip(clip), clip.Duration);
            refs[clip.Name] = r;
            names[r.Id] = clip.Name;
        }
        var player = new AnimationPlayer();
        player.Play(refs["wave"]);                     // 씬 파일의 초기 클립

        void Apply()
        {
            PoseRequest p = player.Pose;
            reference.SetObjectPose(obj, p.ClipA, p.TimeA, p.ClipB, p.TimeB, p.Blend);
        }
        Apply();

        int sceneObject = rig.Scene.FindObject("T");
        Assert.True(sceneObject > 0);
        int frame = 0, finishedFrames = 0, fadingFrames = 0, comparisons = 0;

        void Compare(string when)
        {
            comparisons++;
            float[] expected = reference.GetObjectPose(obj, bones);
            float[] actual = rig.Renderer.GetObjectPose(sceneObject, bones);
            for (int i = 0; i < expected.Length; i++)
                Assert.True(MathF.Abs(expected[i] - actual[i]) < 1e-5f, $"{when} (frame {frame}): matrix element {i} is {actual[i]}, AnimationPlayer says {expected[i]}");

            NodeAnimationState state = rig.Scene.GetNodeAnimation("T");
            Assert.Equal(player.Current is { } c ? names[c.Id] : "", state.Clip);
            Assert.True(MathF.Abs(player.Time - state.Time) < 1e-5f, $"{when} (frame {frame}): time {state.Time} vs {player.Time}");
            Assert.Equal(player.Playing, state.Playing);
            Assert.Equal(player.Speed, state.Speed);
            Assert.Equal(player.IsFading, state.Fading);
            Assert.Equal(player.Loop, state.Loop);
            Assert.Equal(player.Finished, state.Finished);
            if (state.Finished) finishedFrames++;
            if (state.Fading) fadingFrames++;
        }

        Compare("initial");
        foreach (Command c in Script())
        {
            switch (c.Kind)
            {
                case Op.Play:
                    player.Play(refs[c.Clip], c.Value, c.Restart, c.Loop);
                    Apply();                           // 일시정지 중에도 바뀐 포즈가 바로 반영된다(Scene도 같다)
                    rig.Scene.PlayClip("T", c.Clip, c.Value, c.Restart, c.Loop);
                    Compare($"play {c.Clip} fade {c.Value} restart {c.Restart} loop {c.Loop}");
                    break;
                case Op.Stop:
                    player.Clear();
                    Apply();
                    rig.Scene.StopClip("T");
                    Compare("stop");
                    break;
                case Op.Speed:
                    player.Speed = c.Value;
                    rig.Scene.SetClipSpeed("T", c.Value);
                    Compare($"speed {c.Value}");
                    break;
                case Op.Pause:
                    player.Playing = c.Value != 0;
                    rig.Scene.SetClipPlaying("T", c.Value != 0);
                    Compare($"playing {c.Value != 0}");
                    break;
                case Op.Step:
                    for (int i = 0; i < c.Count; i++)
                    {
                        frame++;
                        if (player.Playing && player.Current is not null)
                        {
                            player.Update(c.Value);
                            Apply();                   // Scene.Step도 재생 중이고 클립이 있을 때만 포즈를 다시 올린다
                        }
                        rig.Scene.Step(0, 0, c.Value);
                        Compare("step");
                    }
                    break;
            }
        }
        Assert.True(comparisons > 150, $"compared {comparisons} frames/ops");
        Assert.True(finishedFrames > 10 && fadingFrames > 10, $"finished {finishedFrames}, fading {fadingFrames}: the script must exercise one-shot ends and crossfades");
    }

    // ---- C# 래퍼 ----
    [Fact]
    public void EngineScene_ClipWrappers_FollowTheRulesAndReportErrors()
    {
        using var rig = new Rig(AnimatedDocument());
        EngineScene scene = rig.Scene;

        Assert.Equal("wave", scene.GetNodeAnimation("T").Clip);               // 씬 파일의 초기 클립
        Assert.Equal("", scene.GetNodeAnimation("R").Clip);                   // 클립이 없는 스킨 노드는 레스트 포즈
        scene.PlayClip("R", "curl", fadeSeconds: 0.2f, loop: false);
        NodeAnimationState state = scene.GetNodeAnimation("R");
        Assert.Equal(("curl", false, 0f), (state.Clip, state.Loop, state.Time));
        scene.Step(0, 0, 3f);
        Assert.True(scene.GetNodeAnimation("R").Finished);                    // 길이 2초: 끝에서 멈춤
        scene.StopClip("R");
        Assert.Equal("", scene.GetNodeAnimation("R").Clip);
        scene.SetClipSpeed("R", -2f);
        scene.SetClipPlaying("R", false);
        Assert.Equal((-2f, false), (scene.GetNodeAnimation("R").Speed, scene.GetNodeAnimation("R").Playing));

        Assert.Contains("no node named 'Nope'", Assert.Throws<EngineException>(() => scene.PlayClip("Nope", "wave")).Message);
        Assert.Contains("has no skeleton animation", Assert.Throws<EngineException>(() => scene.PlayClip("Floor", "wave")).Message);
        Assert.Contains("no clip named 'nope' in node 'R' (available: wave, curl)", Assert.Throws<EngineException>(() => scene.PlayClip("R", "nope")).Message);
        Assert.Throws<EngineException>(() => scene.PlayClip("R", "Wave"));    // 대소문자 구분
        Assert.Contains("fade seconds", Assert.Throws<EngineException>(() => scene.PlayClip("R", "wave", fadeSeconds: -1)).Message);
        Assert.Contains("fade seconds", Assert.Throws<EngineException>(() => scene.PlayClip("R", "wave", fadeSeconds: 61)).Message);
        Assert.Contains("fade seconds", Assert.Throws<EngineException>(() => scene.PlayClip("R", "wave", fadeSeconds: float.NaN)).Message);
        Assert.Contains("clip speed must be finite", Assert.Throws<EngineException>(() => scene.SetClipSpeed("R", float.PositiveInfinity)).Message);
        Assert.Throws<EngineException>(() => scene.GetNodeAnimation("Floor"));
        Assert.Null(scene.TryGetNodeAnimation("Floor"));                      // 스킨이 아닌 노드/없는 노드는 null
        Assert.Null(scene.TryGetNodeAnimation("Nope"));
    }

    [Fact]
    public void EngineScene_PlayerSpeed_IsTheActualMovement()
    {
        using var rig = new Rig(AnimatedDocument());
        EngineScene scene = rig.Scene;
        Assert.Equal(0f, scene.PlayerSpeed);
        for (int i = 0; i < 5; i++)
            scene.Step(1, 0, 1f / 30);
        Assert.Equal(3f, scene.PlayerSpeed, 2);                               // 샘플 플레이어 속도 3
        scene.Step(0, 0, 1f / 30);
        Assert.Equal(0f, scene.PlayerSpeed);
        scene.Step(1, 0, 1f / 30);
        scene.TeleportPlayer(-5, 3);
        Assert.Equal(0f, scene.PlayerSpeed);                                  // 텔레포트 직후는 0
        scene.Step(1, 0, 0f);
        Assert.Equal(0f, scene.PlayerSpeed);                                  // dt 0
    }

    // ---- 스크립트 컨텍스트 ----
    [Fact]
    public void Context_ClipApi_ReturnsFalseForUnknownNodes_ThrowsForRealErrors_AndReadsNullWhenThereIsNoAnimation()
    {
        using var rig = new Rig(AnimatedDocument());
        bool play = true, stop = true, speed = true, playing = true;
        ClipState? unknown = new ClipState("x", 0, true, 1, false, true, false), notSkinned = unknown, rig0 = null, hero = unknown;
        rig.Runner(new Lambda(c =>
        {
            play = c.PlayClip("Nope", "wave");
            stop = c.StopClip("Nope");
            speed = c.SetClipSpeed("Nope", 1);
            playing = c.SetClipPlaying("Nope", true);
            unknown = c.GetClip("Nope");
            notSkinned = c.GetClip("Floor");
            Assert.True(c.PlayClip("R", "curl", 0.1f));
            rig0 = c.GetClip("R");
            hero = c.GetClip("Hero");
        })).Start();
        Assert.False(play || stop || speed || playing);
        Assert.Null(unknown);
        Assert.Null(notSkinned);
        Assert.Equal("curl", rig0!.Value.Clip);
        Assert.Null(hero);                              // 이 문서의 플레이어(Hero)는 큐브라 스킨이 아니다

        Assert.Contains("has no skeleton animation", Assert.Throws<ScriptException>(rig.Runner(new Lambda(c => c.PlayClip("Floor", "wave"))).Start).Message);
        Assert.Contains("no clip named 'nope'", Assert.Throws<ScriptException>(rig.Runner(new Lambda(c => c.PlayClip("R", "nope"))).Start).Message);
        Assert.Contains("fade seconds", Assert.Throws<ScriptException>(rig.Runner(new Lambda(c => c.PlayClip("R", "wave", 100))).Start).Message);
        Assert.Contains("clip speed must be finite", Assert.Throws<ScriptException>(rig.Runner(new Lambda(c => c.SetClipSpeed("R", float.NaN))).Start).Message);
    }

    [Fact]
    public void Context_IsSplitIntoWorldObjectAndAnimationApis_AndPlayerSpeedIsReadThroughIt()
    {
        using var rig = new Rig(AnimatedDocument());
        float speedWhileWalking = -1;
        rig.Scene.Step(1, 0, 1f / 30);
        rig.Scene.Step(1, 0, 1f / 30);
        rig.Runner(new Lambda(c =>
        {
            Assert.IsAssignableFrom<IWorldApi>(c);          // IScriptContext = IWorldApi + IObjectApi + IAnimationApi + Log
            Assert.IsAssignableFrom<IObjectApi>(c);
            Assert.IsAssignableFrom<IAnimationApi>(c);
            IWorldApi world = c;
            speedWhileWalking = world.PlayerSpeed;
        })).Start();
        Assert.Equal(3f, speedWhileWalking, 2);
    }
}
