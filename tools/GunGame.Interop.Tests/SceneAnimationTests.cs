using GunGame.Interop;
using GunGame.Scene;

namespace GunGame.Interop.Tests;

public class SceneAnimationTests
{
    private sealed class Fixture : IDisposable
    {
        public TestAssets Assets = new();
        public SceneDocument Doc = new();
        public EngineRenderer Engine = new(96, 72, useWarp: true);
        public SceneSync Sync;
        public SceneNode Node;

        public Fixture(string? clip = "bend", bool playing = true, Action<GltfBuilder>? configure = null)
        {
            var builder = new GltfBuilder();
            builder.Animations.Add(("idle", 0f, 0f));
            configure?.Invoke(builder);
            Node = new SceneNode { MeshPath = builder.Write(Assets.Directory, "rig"), AnimationClip = clip, AnimationPlaying = playing };
            Doc.Add(Node);
            Sync = new SceneSync(Engine);
            Sync.Attach(Doc);
            Engine.SetCamera(new Vec3(0, 1.2f, 6), new Vec3(0, 1.2f, 0), 0.8f);
        }

        public byte[] Frame()
        {
            Engine.Render();
            return Engine.Pixels.ToArray();
        }

        public void Dispose()
        {
            Sync.Dispose();
            Engine.Dispose();
            Assets.Dispose();
        }
    }

    [Fact]
    public void GlbNode_LoadsClipsAndExposesNames()
    {
        using var f = new Fixture();
        Assert.Equal(["bend", "idle"], f.Sync.GetClipNames(f.Node));
        Assert.Empty(f.Sync.Errors);
        Assert.Equal(0f, f.Sync.GetAnimationTime(f.Node));
    }

    [Fact]
    public void Tick_AdvancesPlayingNodeAndRaisesPoseChangedOnly()
    {
        using var f = new Fixture();
        byte[] atStart = f.Frame();
        int poseChanged = 0, changed = 0;
        f.Sync.PoseChanged += () => poseChanged++;
        f.Sync.Changed += () => changed++;  // 구조/속성 변경용 이벤트: 애니메이션 틱마다 오면 상태 표시줄·목록이 매 프레임 갱신된다

        f.Sync.Tick(0.5f);
        Assert.Equal(1, poseChanged);
        Assert.Equal(0, changed);
        Assert.Equal(0.5f, f.Sync.GetAnimationTime(f.Node)!.Value, 4);
        Assert.NotEqual(atStart, f.Frame());
    }

    [Fact]
    public void Tick_DoesNothingWhenPausedOrNoClip()
    {
        using var paused = new Fixture(playing: false);
        byte[] before = paused.Frame();
        int changed = 0;
        paused.Sync.PoseChanged += () => changed++;
        paused.Sync.Tick(0.5f);
        Assert.Equal(0, changed);
        Assert.Equal(before, paused.Frame());

        using var noClip = new Fixture(clip: null);
        noClip.Sync.Tick(0.5f);
        Assert.Null(noClip.Sync.GetAnimationTime(noClip.Node));
    }

    [Fact]
    public void Speed_ScalesPlaybackAndTogglingPlayingResumes()
    {
        using var f = new Fixture();
        f.Node.AnimationSpeed = 2;
        f.Sync.Tick(0.25f);
        Assert.Equal(0.5f, f.Sync.GetAnimationTime(f.Node)!.Value, 4);

        f.Node.AnimationPlaying = false;
        f.Sync.Tick(0.25f);
        Assert.Equal(0.5f, f.Sync.GetAnimationTime(f.Node)!.Value, 4);
        f.Node.AnimationPlaying = true;
        f.Sync.Tick(0.1f);
        Assert.Equal(0.7f, f.Sync.GetAnimationTime(f.Node)!.Value, 4);
    }

    [Fact]
    public void SwitchingClip_CrossfadesAndEndsIdenticalToFreshPlayback()
    {
        using var switched = new Fixture(clip: "bend");
        switched.Sync.Tick(0.3f);
        switched.Node.AnimationClip = "idle";
        switched.Sync.Tick(0.25f);  // 크로스페이드(0.2초)가 끝난 뒤
        byte[] afterSwitch = switched.Frame();

        using var fresh = new Fixture(clip: "idle");
        fresh.Sync.Tick(0.25f);
        Assert.Equal(fresh.Frame(), afterSwitch);
    }

    [Fact]
    public void UnknownClip_ReportsErrorAndFallsBackToRestPose()
    {
        using var f = new Fixture(clip: "bend");
        f.Node.AnimationClip = "nope";
        Assert.Contains(f.Sync.Errors, e => e.Contains("nope"));
        Assert.Null(f.Sync.GetAnimationTime(f.Node));

        f.Node.AnimationClip = "bend";
        Assert.Empty(f.Sync.Errors);
    }

    [Fact]
    public void StructureChange_KeepsAnimationTime()
    {
        using var f = new Fixture();
        f.Sync.Tick(0.3f);
        f.Doc.Add(new SceneNode { Name = "Other" });  // Rebuild 발생
        Assert.Equal(0.3f, f.Sync.GetAnimationTime(f.Node)!.Value, 4);
    }

    [Fact]
    public void SwappingMeshBetweenSkinnedAndStatic_Works()
    {
        using var f = new Fixture();
        f.Sync.Tick(0.2f);
        string obj = f.Assets.WriteCubeObj();
        string glb = f.Node.MeshPath!;

        f.Node.MeshPath = obj;  // 스키닝 → 정적
        Assert.Empty(f.Sync.Errors);
        Assert.Empty(f.Sync.GetClipNames(f.Node));
        Assert.Null(f.Sync.GetAnimationTime(f.Node));
        f.Frame();

        f.Node.MeshPath = glb;  // 정적 → 스키닝
        Assert.Empty(f.Sync.Errors);
        Assert.Equal(["bend", "idle"], f.Sync.GetClipNames(f.Node));
        f.Frame();
    }

    [Fact]
    public void ImportWarnings_AreSurfaced()
    {
        using var f = new Fixture(configure: b => b.SecondSkin = true);
        Assert.Contains(f.Sync.Warnings, w => w.Contains("multiple skins"));
        Assert.Empty(f.Sync.Errors);

        // 아마추어 변환은 이제 반영되므로 경고가 아니다.
        using var moved = new Fixture(configure: b => b.ArmatureTranslation = [0, 3, 0]);
        Assert.Empty(moved.Sync.Warnings);
        Assert.Empty(moved.Sync.Errors);
    }

    [Fact]
    public void BrokenGlb_ReportsErrorWithoutThrowing()
    {
        using var f = new Fixture();
        string broken = f.Assets.Path_("broken.glb");
        File.WriteAllBytes(broken, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20]);
        f.Node.MeshPath = broken;
        Assert.Single(f.Sync.Errors);
        f.Frame();
    }

    [Fact]
    public void Serializer_RoundTripsAnimationFields_AndOmitsDefaults()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        doc.Add(new SceneNode { Name = "A", MeshPath = assets.Path_("rig.glb"), AnimationClip = "walk", AnimationPlaying = false, AnimationSpeed = 1.5f });
        doc.Add(new SceneNode { Name = "B", MeshPath = assets.Path_("rig.glb") });
        string path = assets.Path_("anim.scene.json");
        SceneSerializer.Save(doc, path);

        string json = File.ReadAllText(path);
        Assert.Equal(1, json.Split("\"clip\"").Length - 1);  // 클립이 없는 노드에는 애니메이션 필드가 없다
        Assert.DoesNotContain("\"speed\": 1,", json);

        SceneDocument loaded = SceneSerializer.Load(path);
        Assert.Equal("walk", loaded.Roots[0].AnimationClip);
        Assert.False(loaded.Roots[0].AnimationPlaying);
        Assert.Equal(1.5f, loaded.Roots[0].AnimationSpeed);
        Assert.Null(loaded.Roots[1].AnimationClip);
        Assert.True(loaded.Roots[1].AnimationPlaying);
        Assert.Equal(1f, loaded.Roots[1].AnimationSpeed);
    }
}
