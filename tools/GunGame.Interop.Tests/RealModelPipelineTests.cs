using GunGame.Interop;
using GunGame.Scene;

namespace GunGame.Interop.Tests;

/// <summary>
/// 실제 외부 모델(assets/external)로 만든 데모 씬이 에디터 경로(SceneSync) -> Bake -> 엔진 런타임까지 이어지는지 확인한다.
/// GGE_WRITE_DEMO=&lt;폴더&gt; 로 실행하면 구운 real_models.ggs를 그 폴더에 써서 gge_player로 눈으로 확인할 수 있다.
/// </summary>
public class RealModelPipelineTests
{
    private static string DemoScenePath() => Path.Combine(Path.GetDirectoryName(RealGltfModelTests.ModelPath("CesiumMan"))!, "..", "demo", "real_models.scene.json");

    [Fact]
    public void DemoScene_LoadsInTheEditorPath_WithOnlyTheDocumentedWarnings()
    {
        SceneDocument doc = SceneSerializer.Load(DemoScenePath());
        using var engine = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(engine);
        sync.Attach(doc);

        Assert.Empty(sync.Errors);
        // 트럭은 재질 4개와 노드 애니메이션(바퀴)을 가져오지 못한다. 다른 모델에는 경고가 없어야 한다.
        Assert.Equal(2, sync.Warnings.Count);
        Assert.All(sync.Warnings, w => Assert.Contains("Truck", w));
        Assert.Contains(sync.Warnings, w => w.Contains("uses 4 materials"));
        Assert.Contains(sync.Warnings, w => w.Contains("animations ignored because the file has no skin"));
    }

    [Fact]
    public void DemoScene_Bakes_AndRunsInTheEngineRuntime()
    {
        byte[] baked = SceneBaker.BakeToBytes(SceneSerializer.Load(DemoScenePath()));
        Assert.Equal(baked, SceneBaker.BakeToBytes(SceneSerializer.Load(DemoScenePath())));   // 같은 입력이면 같은 바이트

        string? target = Environment.GetEnvironmentVariable("GGE_WRITE_DEMO");
        if (!string.IsNullOrEmpty(target))
        {
            Directory.CreateDirectory(target);
            File.WriteAllBytes(Path.Combine(target, "real_models.ggs"), baked);
        }

        using var assets = new TestAssets();
        string path = assets.Path_("real_models.ggs");
        File.WriteAllBytes(path, baked);
        using var renderer = new EngineRenderer(160, 120, useWarp: true);
        using EngineScene scene = EngineScene.Load(renderer, path);
        Assert.Equal("Stage", scene.ActiveRoomName);
        Assert.Equal(new Vec2(-3, 0), scene.Player!.Value.Position);
        foreach (string node in new[] { "Floor", "Man", "Fox", "Truck", "Rig" })
            Assert.True(scene.FindObject(node) > 0, node);
        renderer.Render();
        byte[] before = renderer.Pixels.ToArray();
        for (int i = 0; i < 30; i++)
            scene.Step(i < 15 ? 1 : 0, 0, 1f / 30f);   // 오른쪽으로 걷다가 선다(스킨 애니메이션 모델이 플레이어)
        renderer.Render();
        Assert.True(scene.Player!.Value.Position.X > -2.5f);
        Assert.NotEqual(before, renderer.Pixels.ToArray());   // 이동과 애니메이션이 화면에 반영된다
    }

    private static int DifferentPixels(byte[] a, byte[] b)
    {
        int count = 0;
        for (int i = 0; i < a.Length; i += 4)
            if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2])
                count++;
        return count;
    }

    [Fact]
    public void DemoScene_ActuallyDrawsEveryModel_AndTheScene()
    {
        using var assets = new TestAssets();
        string path = assets.Path_("real_models.ggs");
        SceneBaker.Bake(SceneSerializer.Load(DemoScenePath()), path);
        using var renderer = new EngineRenderer(160, 120, useWarp: true);
        using EngineScene scene = EngineScene.Load(renderer, path);
        scene.Step(0, 0, 1f / 30f);
        renderer.Render();
        byte[] frame = renderer.Pixels.ToArray();

        // (a) 화면이 배경색 한 가지가 아니다: 바닥과 모델이 상당한 면적을 차지한다.
        int background = 0;
        for (int i = 0; i < frame.Length; i += 4)
            if (frame[i] == frame[0] && frame[i + 1] == frame[1] && frame[i + 2] == frame[2])
                background++;
        Assert.True(background < 0.75 * (frame.Length / 4), $"{background} of {frame.Length / 4} pixels are background");

        // (b) 모델마다 그려진다: 그 오브젝트만 투명하게 하면 화면이 달라진다(텍스처/스킨/조상 변환이 깨져 사라지거나 화면 밖으로 가면 0이 된다).
        foreach (string node in new[] { "Man", "Fox", "Truck", "Rig" })
        {
            int id = scene.FindObject(node);
            renderer.SetObjectAlpha(id, 0);
            renderer.Render();
            int changed = DifferentPixels(frame, renderer.Pixels.ToArray());
            renderer.SetObjectAlpha(id, 1);
            Assert.True(changed >= 15, $"{node} covers only {changed} pixels");
        }
    }

    [Theory]
    [InlineData(0f, -1f, 0f)]     // 아래(S) = 카메라 쪽(+Z)으로 걷는다: yaw 0 = 모델 앞쪽(+Z)이 이동 방향
    [InlineData(1f, 0f, 90f)]     // 오른쪽(D) = +X
    [InlineData(-1f, 0f, -90f)]   // 왼쪽(A) = -X
    [InlineData(0f, 1f, 180f)]    // 위(W) = 카메라에서 멀어지는 -Z
    public void PlayerYaw_TurnsTheModelsFront_TowardTheMovementDirection(float inputX, float inputY, float expectedDegrees)
    {
        // 열린 결정 "모델 앞쪽 +Z 가정"의 근거: glTF 규격은 앞쪽이 +Z이고, 헤드리스 스크린샷으로 CesiumMan이 걸어가는 방향을 향하는 것을 확인했다
        // (S로 걸으면 얼굴이 카메라 쪽으로 보이고 W로 걸으면 뒤통수가 보인다). 여기서는 yaw와 이동 방향의 관계(atan2(x, z))를 고정한다.
        using var assets = new TestAssets();
        string path = assets.Path_("real_models.ggs");
        SceneBaker.Bake(SceneSerializer.Load(DemoScenePath()), path);
        using var renderer = new EngineRenderer(96, 72, useWarp: true);
        using EngineScene scene = EngineScene.Load(renderer, path);
        for (int i = 0; i < 5; i++)
            scene.Step(inputX, inputY, 1f / 30f);
        float yawDegrees = scene.Player!.Value.Yaw * 180f / MathF.PI;
        float difference = MathF.Abs(MathF.IEEERemainder(yawDegrees - expectedDegrees, 360f));
        Assert.True(difference < 0.5f, $"yaw {yawDegrees:0.#} degrees, expected {expectedDegrees}");
    }
}
