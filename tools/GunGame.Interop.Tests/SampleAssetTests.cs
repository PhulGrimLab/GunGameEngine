using GunGame.Interop;
using GunGame.Scene;

namespace GunGame.Interop.Tests;

/// <summary>
/// 저장소의 샘플 에셋(assets/samples)이 항상 유효한지 확인한다.
/// GGE_WRITE_SAMPLES=&lt;폴더&gt; 로 실행하면 glb 샘플을 다시 생성한다(골든 이미지의 GGE_UPDATE_GOLDEN과 같은 방식).
/// </summary>
public class SampleAssetTests
{
    private static string? FindSamples()
    {
        for (string? dir = AppContext.BaseDirectory; dir is not null; dir = Path.GetDirectoryName(dir))
        {
            string candidate = Path.Combine(dir, "assets", "samples");
            if (Directory.Exists(candidate))
                return candidate;
        }
        return null;
    }

    [Fact]
    public void WriteSamples_WhenRequested()
    {
        string? target = Environment.GetEnvironmentVariable("GGE_WRITE_SAMPLES");
        if (string.IsNullOrEmpty(target))
            return;

        Directory.CreateDirectory(target);
        var builder = new GltfBuilder { Tube = true };
        builder.Animations.Clear();
        builder.KeyedAnimations.Add(("wave", [0f, 0.5f, 1f, 1.5f, 2f],
            [(1, [0f, 0.7f, 0f, -0.7f, 0f]), (2, [0.4f, 0.7f, -0.4f, -0.7f, 0.4f])]));
        builder.KeyedAnimations.Add(("curl", [0f, 1f, 2f],
            [(1, [0f, 1.0f, 0f]), (2, [0f, 1.2f, 0f])]));
        builder.Write(target, "tentacle");
    }

    [Fact]
    public void TentacleSample_ImportsWithClipsAndLoadsInSampleScene()
    {
        // 이 테스트는 저장소 안에서만 돈다(build.cmd). 샘플을 못 찾으면 조용히 통과하지 말고 실패해야 경로가 깨진 것을 알 수 있다.
        string? samples = FindSamples();
        Assert.True(samples is not null, "assets/samples was not found above the test binaries");
        Assert.True(File.Exists(Path.Combine(samples, "tentacle.glb")), "assets/samples/tentacle.glb is missing");

        ImportedModel model = GltfImporter.Import(Path.Combine(samples, "tentacle.glb"));
        Assert.NotNull(model.Skeleton);
        Assert.Equal(3, model.Skeleton.BoneCount);
        Assert.Equal(["wave", "curl"], model.Clips.Select(c => c.Name));
        Assert.Empty(model.Warnings);

        SceneDocument scene = SceneSerializer.Load(Path.Combine(samples, "sample.scene.json"));
        using var engine = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(engine);
        sync.Attach(scene);
        Assert.Empty(sync.Errors);
        Assert.Empty(sync.Warnings);
        Assert.Contains(scene.Nodes(), n => n.MeshPath?.EndsWith("tentacle.glb") == true && n.AnimationClip == "wave");
    }
}
