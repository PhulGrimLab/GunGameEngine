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

        // 독립 플레이어/엔진 테스트가 쓰는 구운 샘플. 씬 JSON이나 형식이 바뀌면 다시 만들어야 한다(아래 BakedPlaySample_IsUpToDate가 알려 준다).
        string? samples = FindSamples();
        if (samples is not null)
            File.WriteAllBytes(Path.Combine(target, "play.ggs"), SceneBaker.BakeToBytes(SceneSerializer.Load(Path.Combine(samples, "play.scene.json"))));
    }

    [Fact]
    public void BakedPlaySample_IsUpToDate()
    {
        // assets/samples/play.ggs는 play.scene.json을 구운 결과여야 한다(C++ 엔진 테스트와 플레이어가 이 파일을 그대로 읽는다).
        // 어긋났다면: GGE_WRITE_SAMPLES=<assets/samples 폴더>로 이 테스트 클래스를 실행해 다시 만든다.
        string? samples = FindSamples();
        Assert.True(samples is not null, "assets/samples was not found above the test binaries");
        string baked = Path.Combine(samples, "play.ggs");
        Assert.True(File.Exists(baked), "assets/samples/play.ggs is missing (regenerate with GGE_WRITE_SAMPLES)");

        byte[] expected = SceneBaker.BakeToBytes(SceneSerializer.Load(Path.Combine(samples, "play.scene.json")));
        Assert.True(expected.AsSpan().SequenceEqual(File.ReadAllBytes(baked)), "play.ggs does not match play.scene.json: regenerate it with GGE_WRITE_SAMPLES");
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
