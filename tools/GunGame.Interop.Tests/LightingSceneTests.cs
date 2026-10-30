using System.Text.Json.Nodes;
using GunGame.Interop;
using GunGame.Scene;

namespace GunGame.Interop.Tests;

/// <summary>7단계 씬 쪽: 조명/알파/그림자 데이터의 저장·로드, 검증, 그리고 SceneSync가 엔진에 반영한 결과(화면 값으로 확인).</summary>
public class LightingSceneTests
{
    private static string WriteScene(TestAssets assets, string json, string name = "lit.json")
    {
        string path = assets.Path_(name);
        File.WriteAllText(path, json);
        return path;
    }

    private static string Reason(string path) => Assert.Throws<SceneFormatException>(() => SceneSerializer.Load(path)).Message;

    private static string LightingScene(string lightingJson) => "{\"version\":1,\"nodes\":[],\"lighting\":" + lightingJson + "}";

    // ---- 저장/로드 ----
    [Fact]
    public void LightingAndNodeFlags_SurviveSaveAndLoad()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        doc.Add(new SceneNode { Name = "glass", Alpha = 0.35f, CastsShadow = true });
        doc.Add(new SceneNode { Name = "plain" });
        LightingData lighting = doc.Lighting;
        lighting.Ambient = new Vec3(0.1f, 0.2f, 0.3f);
        lighting.SunDirection = new Vec3(-1, -2, 0.5f);
        lighting.SunColor = new Vec3(0.9f, 0.8f, 0.7f);
        lighting.PointLights.Add(new PointLightDef { Name = "lamp", Position = new Vec3(1, 2, 3), Color = new Vec3(1, 0.5f, 0.25f), Range = 7.5f });
        lighting.PointLights.Add(new PointLightDef { Name = "lamp2", Position = new Vec3(-4, 1, 0), Range = 2f });
        lighting.ShadowEnabled = true;
        lighting.ShadowPlaneY = -0.25f;
        lighting.ShadowOpacity = 0.75f;

        string path = assets.Path_("saved.json");
        SceneSerializer.Save(doc, path);
        SceneDocument loaded = SceneSerializer.Load(path);

        LightingData l = loaded.Lighting;
        Assert.Equal((lighting.Ambient, lighting.SunDirection, lighting.SunColor), (l.Ambient, l.SunDirection, l.SunColor));
        Assert.Equal(["lamp", "lamp2"], l.PointLights.Select(p => p.Name));
        Assert.Equal((new Vec3(1, 2, 3), new Vec3(1, 0.5f, 0.25f), 7.5f), (l.PointLights[0].Position, l.PointLights[0].Color, l.PointLights[0].Range));
        Assert.Equal(new Vec3(1, 1, 1), l.PointLights[1].Color);
        Assert.Equal((true, -0.25f, 0.75f), (l.ShadowEnabled, l.ShadowPlaneY, l.ShadowOpacity));
        Assert.Equal((0.35f, true), (loaded.Roots[0].Alpha, loaded.Roots[0].CastsShadow));
        Assert.Equal((1f, false), (loaded.Roots[1].Alpha, loaded.Roots[1].CastsShadow));
    }

    [Fact]
    public void DefaultsAreNotWritten_AndOldFilesLoadWithDefaultLighting()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        doc.Add(new SceneNode { Name = "n" });
        string path = assets.Path_("default.json");
        SceneSerializer.Save(doc, path);

        JsonNode root = JsonNode.Parse(File.ReadAllText(path))!;
        Assert.Null(root["lighting"]);
        Assert.Null(root["nodes"]![0]!["alpha"]);
        Assert.Null(root["nodes"]![0]!["castsShadow"]);

        SceneDocument old = SceneSerializer.Load(WriteScene(assets, "{\"version\":1,\"nodes\":[{\"name\":\"n\"}]}", "old.json"));
        Assert.True(old.Lighting.IsDefault);
        Assert.Equal((1f, false), (old.Roots[0].Alpha, old.Roots[0].CastsShadow));
    }

    [Fact]
    public void AnySingleNonDefaultLightingValue_IsWritten()
    {
        using var assets = new TestAssets();
        var changes = new (string Name, Action<LightingData> Change)[]
        {
            ("ambient", l => l.Ambient = new Vec3(0, 0, 0)),
            ("sun direction", l => l.SunDirection = new Vec3(0, -1, 0)),
            ("sun color", l => l.SunColor = new Vec3(1, 1, 1)),
            ("point light", l => l.PointLights.Add(new PointLightDef())),
            ("shadow enabled", l => l.ShadowEnabled = true),
            ("shadow plane", l => l.ShadowPlaneY = 1),
            ("shadow opacity", l => l.ShadowOpacity = 0.9f),
        };
        foreach ((string name, Action<LightingData> change) in changes)
        {
            var doc = new SceneDocument();
            change(doc.Lighting);
            Assert.False(doc.Lighting.IsDefault, name);
            string path = assets.Path_(name.Replace(' ', '_') + ".json");
            SceneSerializer.Save(doc, path);
            Assert.NotNull(JsonNode.Parse(File.ReadAllText(path))!["lighting"]);
            Assert.False(SceneSerializer.Load(path).Lighting.IsDefault, name);
        }
    }

    [Fact]
    public void Shadow_WithOnlyPlaneOrOpacityChanged_KeepsTheEnabledFlagAndValues()
    {
        // enabled=false라도 평면/불투명도를 바꿔 둔 값은 잃지 않는다(켰을 때 그대로 쓰려고).
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        doc.Lighting.ShadowPlaneY = 2f;
        doc.Lighting.ShadowOpacity = 0.9f;
        string path = assets.Path_("shadow_off.json");
        SceneSerializer.Save(doc, path);

        LightingData loaded = SceneSerializer.Load(path).Lighting;
        Assert.Equal((false, 2f, 0.9f), (loaded.ShadowEnabled, loaded.ShadowPlaneY, loaded.ShadowOpacity));
    }

    // ---- 검증 ----
    [Fact]
    public void BadLighting_IsRejectedWithItsOwnReason()
    {
        using var assets = new TestAssets();
        var cases = new (string Json, string Expected)[]
        {
            ("{\"ambient\":[-0.1,0,0]}", "'ambient' must not be negative"),
            ("{\"ambient\":[1,2]}", "'lighting 'ambient'' must be three finite numbers"),
            ("{\"sun\":{\"direction\":[0,0,0]}}", "sun 'direction' must not be zero"),
            ("{\"sun\":{\"direction\":[1,2]}}", "sun 'direction'"),
            ("{\"sun\":{\"color\":[1,-1,1]}}", "sun 'color' must not be negative"),
            ("{\"points\":[{\"name\":\"a\"}]}", "needs a 'position'"),
            ("{\"points\":[{\"name\":\"a\",\"position\":[0,0,0],\"range\":0}]}", "'range' must be a positive finite number"),
            ("{\"points\":[{\"name\":\"a\",\"position\":[0,0,0],\"range\":-3}]}", "'range' must be a positive finite number"),
            ("{\"points\":[{\"name\":\"a\",\"position\":[0,0,0],\"color\":[1,1,-1]}]}", "color must not be negative"),
            ("{\"points\":[null]}", "null entry"),
            ("{\"shadow\":{\"opacity\":1.5}}", "'opacity' must be a number between 0 and 1"),
            ("{\"shadow\":{\"opacity\":-0.5}}", "'opacity' must be a number between 0 and 1"),
        };
        foreach ((string json, string expected) in cases)
            Assert.Contains(expected, Reason(WriteScene(assets, LightingScene(json))));

        // 점광원 개수: 8개는 되고 9개는 안 된다.
        string Points(int n) => "{\"points\":[" + string.Join(",", Enumerable.Range(0, n).Select(i => $"{{\"name\":\"p{i}\",\"position\":[{i},0,0]}}")) + "]}";
        Assert.Equal(8, SceneSerializer.Load(WriteScene(assets, LightingScene(Points(8)), "eight.json")).Lighting.PointLights.Count);
        Assert.Contains("at most 8", Reason(WriteScene(assets, LightingScene(Points(9)), "nine.json")));

        foreach ((string alpha, string name) in new[] { ("1.5", "above"), ("-0.1", "below") })
            Assert.Contains("'alpha' must be a number between 0 and 1",
                Reason(WriteScene(assets, "{\"version\":1,\"nodes\":[{\"name\":\"n\",\"alpha\":" + alpha + "}]}", name + ".json")));
    }

    [Fact]
    public void Alpha_AllowsTheBoundaries0And1()
    {
        using var assets = new TestAssets();
        foreach (float alpha in new[] { 0f, 1f })
        {
            SceneDocument doc = SceneSerializer.Load(WriteScene(assets, "{\"version\":1,\"nodes\":[{\"name\":\"n\",\"alpha\":" + alpha.ToString(System.Globalization.CultureInfo.InvariantCulture) + "}]}", $"a{alpha}.json"));
            Assert.Equal(alpha, doc.Roots[0].Alpha);
        }
    }

    // ---- SceneSync: 엔진에 반영된 결과를 화면 값으로 확인 ----
    private static (int B, int G, int R) CenterBgr(EngineRenderer r)
    {
        r.SetCamera(new Vec3(0, 0, 8), new Vec3(0, 0, 0), 0.8f);
        r.Render();
        int offset = (r.Height / 2) * r.Stride + (r.Width / 2) * 4;
        return (r.Pixels[offset], r.Pixels[offset + 1], r.Pixels[offset + 2]);
    }

    private static int Level(float v) => (int)MathF.Round(v * 255);

    /// <summary>뒤에 큰 큐브(앞면 z=0)와 앞에 작은 큐브(앞면 z=3)가 화면 중앙에 겹친다. 앞 큐브는 파랑, 뒤 큐브는 빨강.</summary>
    private static (SceneDocument Doc, SceneNode Back, SceneNode Front) TwoCubes(TestAssets assets)
    {
        var doc = new SceneDocument();
        string cube = assets.WriteCubeObj();
        var back = new SceneNode { Name = "back", MeshPath = cube, Position = new Vec3(0, 0, -3), Scale = new Vec3(3, 3, 3), Color = new Vec3(1, 0, 0) };
        var front = new SceneNode { Name = "front", MeshPath = cube, Position = new Vec3(0, 0, 2), Scale = new Vec3(1, 1, 1), Color = new Vec3(0, 0, 1) };
        doc.Add(back);
        doc.Add(front);
        doc.Lighting.Ambient = new Vec3(1, 1, 1);   // 밝기 1: 출력 = 오브젝트 색
        doc.Lighting.SunColor = new Vec3(0, 0, 0);
        return (doc, back, front);
    }

    [Fact]
    public void NodeAlpha_BlendsOnScreen_AndFollowsPropertyChanges()
    {
        using var assets = new TestAssets();
        (SceneDocument doc, _, SceneNode front) = TwoCubes(assets);
        front.Alpha = 0.25f;
        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);
        Assert.Empty(sync.Errors);

        (int b, int g, int red) = CenterBgr(r);
        Assert.InRange(red, Level(0.75f) - 2, Level(0.75f) + 2);
        Assert.InRange(b, Level(0.25f) - 2, Level(0.25f) + 2);
        Assert.Equal(0, g);

        front.Alpha = 1f;       // 속성 변경이 바로 엔진에 반영된다(구조 재빌드 없이)
        Assert.Equal((255, 0, 0), CenterBgr(r));
        front.Alpha = 0f;
        Assert.Equal((0, 0, 255), CenterBgr(r));
    }

    [Fact]
    public void LightingFromTheDocument_IsAppliedAtAttach_AndReplacedByTheNextDocument()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        doc.Add(new SceneNode { Name = "cube", MeshPath = assets.WriteCubeObj(), Scale = new Vec3(3, 3, 3), Color = new Vec3(1, 1, 1) });
        doc.Lighting.Ambient = new Vec3(0.1f, 0.2f, 0.3f);
        doc.Lighting.SunDirection = new Vec3(0, 0, -1);   // 큐브 앞면(+Z)에 정면
        doc.Lighting.SunColor = new Vec3(0.5f, 0.25f, 0);

        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);
        (int b, int g, int red) = CenterBgr(r);
        Assert.InRange(red, Level(0.6f) - 2, Level(0.6f) + 2);
        Assert.InRange(g, Level(0.45f) - 2, Level(0.45f) + 2);
        Assert.InRange(b, Level(0.3f) - 2, Level(0.3f) + 2);

        // 기본 조명의 다른 문서를 붙이면 이전 문서의 조명이 남지 않는다.
        var plain = new SceneDocument();
        plain.Add(new SceneNode { Name = "cube", MeshPath = assets.WriteCubeObj(), Scale = new Vec3(3, 3, 3), Color = new Vec3(1, 1, 1) });
        sync.Attach(plain);
        float shade = 0.25f + 0.75f * (0.6f / MathF.Sqrt(0.4f * 0.4f + 1.0f + 0.6f * 0.6f));
        (_, _, int defaultRed) = CenterBgr(r);
        Assert.InRange(defaultRed, Level(shade) - 2, Level(shade) + 2);
    }

    [Fact]
    public void PointLights_AreRecreatedOnEveryApply_NotAccumulated()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        doc.Add(new SceneNode { Name = "wall", MeshPath = assets.WriteCubeObj(), Scale = new Vec3(3, 3, 3), Color = new Vec3(1, 1, 1) });
        doc.Lighting.Ambient = new Vec3(0, 0, 0);
        doc.Lighting.SunColor = new Vec3(0, 0, 0);
        doc.Lighting.PointLights.Add(new PointLightDef { Position = new Vec3(0, 0, 5), Color = new Vec3(1, 1, 1), Range = 10 });
        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);
        (int b1, _, _) = CenterBgr(r);
        Assert.True(b1 > 0);  // 점광원이 앞면을 비춘다

        for (int i = 0; i < 20; i++)
            sync.ApplyLighting();  // 8개 한도를 넘게 누적되면 오류가 나거나 더 밝아진다
        Assert.Empty(sync.Errors);
        Assert.Equal(b1, CenterBgr(r).B);

        doc.Lighting.PointLights.Clear();
        sync.ApplyLighting();
        Assert.Equal(0, CenterBgr(r).B);
    }

    [Fact]
    public void LightingTheEngineRefuses_IsReportedInErrors()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        for (int i = 0; i < 9; i++)  // 파일에서는 걸러지지만 코드로 직접 넣을 수는 있다
            doc.Lighting.PointLights.Add(new PointLightDef { Position = new Vec3(i, 1, 0) });
        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);

        Assert.Contains(sync.Errors, e => e.StartsWith("lighting:") && e.Contains("too many point lights"));
    }

    [Fact]
    public void CastsShadow_DarkensTheFloorOnlyWhenTheSceneEnablesShadows()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        string cube = assets.WriteCubeObj();
        var floor = new SceneNode { Name = "floor", MeshPath = cube, Position = new Vec3(0, -1, 0), Scale = new Vec3(6, 1, 6), Color = new Vec3(1, 1, 1) };
        var caster = new SceneNode { Name = "caster", MeshPath = cube, Position = new Vec3(0, 3, 0), Scale = new Vec3(0.5f, 0.5f, 0.5f), Color = new Vec3(1, 1, 1), CastsShadow = true };
        doc.Add(floor);
        doc.Add(caster);
        doc.Lighting.SunDirection = new Vec3(0, -1, 0);

        using var r = new EngineRenderer(96, 72, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);
        // 위에서 비스듬히 본다: 바닥(윗면 y=0)의 원점 부근이 화면 중앙에 오도록 한다.
        byte[] Frame()
        {
            r.SetCamera(new Vec3(0, 7, 6), new Vec3(0, 0, 0), 0.8f);
            r.Render();
            return r.Pixels.ToArray();
        }
        byte[] none = Frame();

        doc.Lighting.ShadowEnabled = true;
        sync.ApplyLighting();
        byte[] shadowed = Frame();
        Assert.NotEqual(none, shadowed);
        int center = (r.Height / 2) * r.Stride + (r.Width / 2) * 4;
        Assert.True(shadowed[center + 2] < none[center + 2] - 40, "the floor under the caster should be darker");

        caster.CastsShadow = false;    // 속성 변경이 바로 반영된다
        Assert.Equal(none, Frame());
        caster.CastsShadow = true;
        Assert.Equal(shadowed, Frame());
    }
}

/// <summary>샘플 씬(assets/samples/lit.scene.json): 조명/알파/그림자 기능을 한 장면에서 보여 준다.</summary>
public class LitSampleSceneTests
{
    private static string SamplePath()
    {
        for (string? dir = AppContext.BaseDirectory; dir is not null; dir = Path.GetDirectoryName(dir))
        {
            string candidate = Path.Combine(dir, "assets", "samples", "lit.scene.json");
            if (File.Exists(candidate))
                return candidate;
        }
        throw new FileNotFoundException("assets/samples/lit.scene.json not found");
    }

    private static byte[] Render(SceneDocument doc)
    {
        using var r = new EngineRenderer(160, 120, useWarp: true);
        using var sync = new SceneSync(r);
        sync.Attach(doc);
        Assert.Empty(sync.Errors);
        r.SetCamera(new Vec3(0, 5, 9), new Vec3(0, 0.5f, 0), 0.8f);
        r.Render();
        return r.Pixels.ToArray();
    }

    [Fact]
    public void LoadsWithEveryFeature_AndRendersDifferentlyThanDefaultLighting()
    {
        SceneDocument doc = SceneSerializer.Load(SamplePath());
        Assert.Equal(["Red", "Blue"], doc.Lighting.PointLights.Select(p => p.Name));
        Assert.True(doc.Lighting.ShadowEnabled);
        Assert.Equal([true, true], doc.Roots.Where(n => n.CastsShadow).Select(_ => true));
        Assert.Equal(0.5f, doc.Roots.Single(n => n.Name == "Glass").Alpha);

        byte[] lit = Render(doc);
        SceneDocument plain = SceneSerializer.Load(SamplePath());
        plain.Lighting = new LightingData();
        foreach (SceneNode node in plain.Roots)
        {
            node.Alpha = 1f;
            node.CastsShadow = false;
        }
        Assert.NotEqual(lit, Render(plain));
    }
}
