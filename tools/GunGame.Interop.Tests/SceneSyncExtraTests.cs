using GunGame.Interop;
using GunGame.Scene;

namespace GunGame.Interop.Tests;

/// <summary>커버리지에서 비어 있던 SceneSync 경로: .ggm 메시 로드, 애니메이션 노드를 지울 때의 재생 상태 정리.</summary>
public class SceneSyncExtraTests
{
    private static byte[] Frame(EngineRenderer r)
    {
        r.SetCamera(new Vec3(6, 5, 9), new Vec3(0, 0, 0), 0.8f);
        r.Render();
        return r.Pixels.ToArray();
    }

    private static byte[] RenderMesh(string meshPath)
    {
        var doc = new SceneDocument();
        doc.Add(new SceneNode { Name = "n", MeshPath = meshPath, Color = new Vec3(0.9f, 0.6f, 0.3f) });
        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);
        Assert.Empty(sync.Errors);
        return Frame(r);
    }

    [Fact]
    public void GgmMesh_RendersLikeItsObjSource()
    {
        using var assets = new TestAssets();
        string obj = assets.WriteCubeObj("cube.obj");
        string ggm = assets.Path_("cube.ggm");
        EngineRenderer.ConvertObjToGgm(obj, ggm);

        byte[] fromObj = RenderMesh(obj);
        byte[] fromGgm = RenderMesh(ggm);
        Assert.Equal(fromObj, fromGgm);
        // 빈 씬과는 다르다: 큐브가 실제로 그려졌다
        using var r = new EngineRenderer(96, 72, useWarp: true);
        Assert.NotEqual(Frame(r), fromGgm);
    }

    [Fact]
    public void RemovingAnAnimatedNode_ClearsItsPlayback()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        var animated = new SceneNode { Name = "rig", MeshPath = new GltfBuilder().Write(assets.Directory, "rig"), AnimationClip = "bend", AnimationPlaying = true };
        var other = new SceneNode { Name = "other", MeshPath = assets.WriteCubeObj() };
        doc.Add(animated);
        doc.Add(other);
        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);
        int poses = 0;
        sync.PoseChanged += () => poses++;

        sync.Tick(0.2f);
        Assert.Equal(1, poses);
        Assert.NotNull(sync.GetAnimationTime(animated));

        doc.Remove(animated);                       // 구조 변경: 재생 상태가 남아 있으면 안 된다
        Assert.Null(sync.GetAnimationTime(animated));
        poses = 0;
        sync.Tick(0.2f);
        Assert.Equal(0, poses);                     // 재생 중인 노드가 없으므로 다시 그릴 것도 없다
        Assert.Empty(sync.Errors);
    }

    [Fact]
    public void AnimatedNodeThatLosesItsClip_StopsAnimating()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        var node = new SceneNode { Name = "rig", MeshPath = new GltfBuilder().Write(assets.Directory, "rig"), AnimationClip = "bend", AnimationPlaying = true };
        doc.Add(node);
        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);
        Assert.NotNull(sync.GetAnimationTime(node));

        node.AnimationClip = null;                  // 레스트 포즈로
        Assert.Null(sync.GetAnimationTime(node));
        int poses = 0;
        sync.PoseChanged += () => poses++;
        sync.Tick(0.2f);
        Assert.Equal(0, poses);
    }
}
