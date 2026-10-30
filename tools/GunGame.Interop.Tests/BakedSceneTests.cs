using GunGame.Interop;
using GunGame.Scene;

namespace GunGame.Interop.Tests;

/// <summary>
/// 구운 씬(.ggs): 씬 -> 굽기 -> 엔진 로드의 왕복, 그리고 같은 입력으로 C# 플레이 모드(PlaySession + SceneSync)와 엔진의 C++ 런타임을
/// 프레임마다 비교하는 교차 검증. 두 구현은 서로 다른 언어로 짜였으므로 같은 결과가 나오면 규칙이 일치한다는 강한 증거다.
/// </summary>
public class BakedSceneTests
{
    private const float FrameSeconds = 1f / 30f;

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

    private static string Bake(TestAssets assets, SceneDocument doc, string name = "scene.ggs")
    {
        string path = assets.Path_(name);
        SceneBaker.Bake(doc, path);
        return path;
    }

    // ---- 왕복과 재현성 ----
    [Fact]
    public void SampleScene_BakesToAFileTheEngineLoads()
    {
        using var assets = new TestAssets();
        SceneDocument doc = PlaySamples.Load();
        string path = Bake(assets, doc);

        using var renderer = new EngineRenderer(96, 72, useWarp: true);
        using EngineScene scene = EngineScene.Load(renderer, path);
        (Vec2 position, float yaw) = scene.Player!.Value;
        Assert.Equal(new Vec2(-4, 0), position);
        Assert.Equal(0f, yaw);
        Assert.Equal("Hall", scene.ActiveRoomName);
        Assert.True(scene.TryPollEvent(out SceneEventData enter));  // 스폰이 StartPad 안: 시작할 때 Enter가 이미 쌓여 있다
        Assert.Equal(new SceneEventData(TriggerEventKind.Enter, 1, "StartPad"), enter);
        Assert.False(scene.TryPollEvent(out _));
        Assert.True(scene.FindObject("Hero") > 0);
        Assert.Equal(0, scene.FindObject("Nobody"));
    }

    [Fact]
    public void Baking_IsDeterministic_AndIgnoresNothingTheSceneSays()
    {
        SceneDocument doc = PlaySamples.Load();
        byte[] a = SceneBaker.BakeToBytes(doc), b = SceneBaker.BakeToBytes(PlaySamples.Load());
        Assert.Equal(a, b);

        doc.Roots.First(n => n.Name == "Floor").Color = new Vec3(0.1f, 0.2f, 0.3f);  // 바뀐 값은 파일에도 반영된다
        Assert.NotEqual(a, SceneBaker.BakeToBytes(doc));
    }

    [Fact]
    public void Baker_AndEngineAgreeOnTheChecksum()
    {
        byte[] bytes = SceneBaker.BakeToBytes(PlaySamples.Load());
        uint stored = BitConverter.ToUInt32(bytes, bytes.Length - 4);
        Assert.Equal(stored, SceneBaker.Crc32(bytes.AsSpan(0, bytes.Length - 4)));
        Assert.Equal(0xCBF43926u, SceneBaker.Crc32("123456789"u8));  // CRC-32/IEEE의 표준 검사값
    }

    // ---- 교차 검증: C# 플레이 모드 == C++ 런타임 ----
    /// <summary>걷는 입력 대본: 오른쪽으로 길게(방 경계를 넘어 카메라가 바뀐다), 대각선, 위, 아래, 멈춤.</summary>
    private static IEnumerable<(float X, float Y)> WalkScript()
    {
        for (int i = 0; i < 160; i++) yield return (1, 0);
        for (int i = 0; i < 40; i++) yield return (0.7f, 0.7f);
        for (int i = 0; i < 40; i++) yield return (0, 1);
        for (int i = 0; i < 60; i++) yield return (-1, -0.5f);
        for (int i = 0; i < 20; i++) yield return (0.1f, 0);  // 데드존 안: 안 움직인다
        for (int i = 0; i < 20; i++) yield return (0, 0);
        for (int i = 0; i < 80; i++) yield return (-1, 0);
    }

    [Fact]
    public void RuntimeMatchesThePlayModeFrameByFrame_PositionYawRoomAndEvents()
    {
        using var assets = new TestAssets();
        SceneDocument doc = PlaySamples.Load();
        string path = Bake(assets, doc);
        using PlaySession session = PlaySession.Start(doc);
        using var renderer = new EngineRenderer(64, 48, useWarp: true);
        using EngineScene scene = EngineScene.Load(renderer, path);

        int frame = 0, eventCount = 0;
        var sessionEvents = new List<PlayEvent>(session.Step(0, 0, 0));  // 시작할 때 쌓인 이벤트(첫 Step에 실려 나온다)
        scene.Step(0, 0, 0);
        var sceneEvents = Drain(scene);
        Assert.Equal(sessionEvents.Select(Describe), sceneEvents.Select(Describe));

        foreach ((float x, float y) in WalkScript())
        {
            frame++;
            sessionEvents = [.. session.Step(x, y, FrameSeconds)];
            scene.Step(x, y, FrameSeconds);
            sceneEvents = Drain(scene);

            (Vec2 position, float yaw) = scene.Player!.Value;
            Assert.True(MathF.Abs(position.X - session.Position.X) < 1e-4f && MathF.Abs(position.Z - session.Position.Z) < 1e-4f,
                        $"frame {frame}: position ({position.X}, {position.Z}) vs ({session.Position.X}, {session.Position.Z})");
            Assert.True(MathF.Abs(yaw - session.Yaw) < 1e-4f, $"frame {frame}: yaw {yaw} vs {session.Yaw}");
            Assert.Equal(session.ActiveRoomName, scene.ActiveRoomName);
            Assert.Equal(sessionEvents.Select(Describe), sceneEvents.Select(Describe));
            eventCount += sceneEvents.Count;
        }
        Assert.True(eventCount >= 4, "the script should walk through both triggers (enter/exit each)");
    }

    private static string Describe(PlayEvent e) => $"{e.Kind} {e.TriggerName} {e.Tag}";
    private static string Describe(SceneEventData e) => $"{e.Kind} {e.TriggerName} {e.Tag}";

    private static List<SceneEventData> Drain(EngineScene scene)
    {
        var events = new List<SceneEventData>();
        while (scene.TryPollEvent(out SceneEventData e))
            events.Add(e);
        return events;
    }

    /// <summary>플레이 씬에 조명/알파/그림자/텍스처/애니메이션 노드를 더한 문서: 런타임이 렌더 결과까지 같은지 보려는 용도.</summary>
    private static SceneDocument RichDocument(TestAssets assets)
    {
        string samples = SamplesDir();
        string cube = assets.WriteCubeObj();
        SceneDocument doc = PlaySamples.Load();
        foreach (SceneNode node in doc.Roots)
            node.MeshPath = cube;  // 임시 폴더의 큐브(샘플 폴더 경로와 무관하게 같은 결과)

        doc.Roots.First(n => n.Name == "Floor").TexturePath = Path.Combine(samples, "checker.png");
        doc.Roots.First(n => n.Name == "Hero").CastsShadow = true;
        doc.Add(new SceneNode { Name = "Glass", MeshPath = cube, Position = new Vec3(-3, 1, 1), Scale = new Vec3(0.8f, 0.8f, 0.05f), Color = new Vec3(0.4f, 0.9f, 0.9f), Alpha = 0.5f });
        doc.Add(new SceneNode
        {
            Name = "Tentacle",
            MeshPath = Path.Combine(samples, "tentacle.glb"),
            Position = new Vec3(-2.5f, 0, -1),
            Color = new Vec3(0.3f, 0.8f, 0.4f),
            AnimationClip = "wave",
            AnimationSpeed = 1.5f,
            CastsShadow = true,
        });
        doc.Add(new SceneNode { Name = "Still", MeshPath = Path.Combine(samples, "tentacle.glb"), Position = new Vec3(-1, 0, -2), AnimationClip = "curl", AnimationPlaying = false });

        LightingData l = doc.Lighting;
        l.Ambient = new Vec3(0.15f, 0.15f, 0.2f);
        l.SunDirection = new Vec3(-0.5f, -1, -0.3f);
        l.SunColor = new Vec3(0.5f, 0.5f, 0.45f);
        l.PointLights.Add(new PointLightDef { Position = new Vec3(-3, 2, 2), Color = new Vec3(1, 0.3f, 0.3f), Range = 6 });
        l.ShadowEnabled = true;
        l.ShadowOpacity = 0.6f;
        return doc;
    }

    [Fact]
    public void RuntimeRendersLikeTheEditorsPlayMode_WithLightingAlphaShadowsTexturesAndAnimation()
    {
        using var assets = new TestAssets();
        SceneDocument doc = RichDocument(assets);
        string path = Bake(assets, doc);

        // A: 에디터 경로(SceneSync + PlaySession 미리보기 + 애니메이션 Tick). B: 구운 파일 + 엔진 런타임.
        using PlaySession session = PlaySession.Start(doc);
        using var a = new EngineRenderer(160, 120, useWarp: true);
        using var sync = new SceneSync(a);
        sync.Attach(doc);
        Assert.Empty(sync.Errors);
        using var b = new EngineRenderer(160, 120, useWarp: true);
        using EngineScene scene = EngineScene.Load(b, path);

        SceneNode hero = doc.Roots.First(n => n.Name == "Hero");
        session.Step(0, 0, 0);
        scene.Step(0, 0, 0);
        int differing = 0;
        for (int frame = 0; frame < 120; frame++)
        {
            (float x, float y) = frame < 60 ? (1f, 0f) : (0f, 1f);
            session.Step(x, y, FrameSeconds);
            sync.Preview(hero, session.PlayerPosition, session.PlayerRotation);
            sync.Tick(FrameSeconds);
            scene.Step(x, y, FrameSeconds);

            if (frame % 20 != 19)
                continue;
            RoomCameraData camera = session.Camera;
            a.SetCamera(camera.Eye, camera.Target, camera.FovY);  // 엔진 런타임은 같은 카메라를 스스로 설정한다
            a.Render();
            b.Render();
            differing += CountDifferingBytes(a.Pixels, b.Pixels);
        }
        Assert.Equal(0, differing);  // 같은 입력/시간이면 화면이 바이트 단위로 같다(애니메이션, 그림자, 알파, 점광원 포함)
    }

    private static int CountDifferingBytes(ReadOnlySpan<byte> a, ReadOnlySpan<byte> b)
    {
        int n = 0;
        for (int i = 0; i < a.Length; i++)
            if (a[i] != b[i]) n++;
        return n;
    }

    // ---- 굽기 실패 ----
    private static SceneDocument MinimalGameplayDocument(TestAssets assets)
    {
        var doc = new SceneDocument();
        doc.Add(new SceneNode { Name = "Hero", MeshPath = assets.WriteCubeObj(), Position = new Vec3(2, 0, 2) });
        doc.Gameplay.Rooms.Add(new RoomDef { Name = "Room", Polygon = [0, 0, 4, 0, 4, 4, 0, 4], Eye = new Vec3(2, 5, 9), Target = new Vec3(2, 0, 2) });
        doc.Gameplay.Player = new PlayerDef { Node = "Hero" };
        return doc;
    }

    [Fact]
    public void BakingFails_WithTheNodeAndReason_AndLeavesAnExistingFileAlone()
    {
        using var assets = new TestAssets();
        string cube = assets.WriteCubeObj();
        string existing = Bake(assets, MinimalGameplayDocument(assets), "keep.ggs");
        byte[] before = File.ReadAllBytes(existing);

        var cases = new (string Name, Action<SceneDocument> Break, string Expected)[]
        {
            ("missing mesh", d => d.Roots[0].MeshPath = assets.Path_("missing.obj"), "node 'Hero': cannot read mesh"),
            ("not a mesh", d => { File.WriteAllText(assets.Path_("bad.obj"), "f 1 2 3"); d.Roots[0].MeshPath = assets.Path_("bad.obj"); }, "node 'Hero': cannot read mesh"),
            ("bad glb", d => { File.WriteAllBytes(assets.Path_("bad.glb"), [1, 2, 3, 4, 5]); d.Roots[0].MeshPath = assets.Path_("bad.glb"); }, "node 'Hero': cannot read mesh"),
            ("missing texture", d => d.Roots[0].TexturePath = assets.Path_("missing.png"), "node 'Hero': cannot read texture"),
            ("empty texture", d => { File.WriteAllBytes(assets.Path_("empty.png"), []); d.Roots[0].TexturePath = assets.Path_("empty.png"); }, "the file is empty"),
            ("clip not in the mesh", d =>
            {
                d.Roots[0].MeshPath = Path.Combine(SamplesDir(), "tentacle.glb");
                d.Roots[0].AnimationClip = "nope";
            }, "clip 'nope' was not found"),
            ("player node missing", d => d.Gameplay.Player!.Node = "Ghost", "player node 'Ghost' was not found"),
            ("player is a child", d =>
            {
                var group = new SceneNode { Name = "Group" };
                d.Move(d.Roots[0], null);
                d.Add(group);
                d.Move(d.Roots[0], group);
            }, "must be a root node"),
            ("no rooms", d => d.Gameplay.Rooms.Clear(), "no gameplay.rooms"),
            ("spawn outside every room", d => d.Gameplay.Player!.Spawn = new Vec2(50, 50), "does not load in the engine"),
            ("degenerate room polygon", d => d.Gameplay.Rooms[0].Polygon = [0, 0, 1, 1, 2, 2], "does not load in the engine"),
        };
        foreach ((string name, Action<SceneDocument> doBreak, string expected) in cases)
        {
            SceneDocument doc = MinimalGameplayDocument(assets);
            doBreak(doc);
            var ex = Assert.Throws<BakeException>(() => SceneBaker.Bake(doc, existing));
            Assert.True(ex.Message.Contains(expected), $"{name}: got '{ex.Message}'");
            Assert.Equal(before, File.ReadAllBytes(existing));  // 실패하면 기존 파일은 그대로다
            Assert.Empty(Directory.GetFiles(assets.Directory, "*.tmp"));  // 임시 파일도 남지 않는다
        }
    }

    [Fact]
    public void BakingWithoutVerification_StillWritesAnInvalidSceneThatTheEngineThenRefuses()
    {
        using var assets = new TestAssets();
        SceneDocument doc = MinimalGameplayDocument(assets);
        doc.Gameplay.Player!.Spawn = new Vec2(50, 50);
        string path = assets.Path_("unchecked.ggs");
        SceneBaker.Bake(doc, path, verify: false);

        using var renderer = new EngineRenderer(64, 48, useWarp: true);
        var ex = Assert.Throws<EngineException>(() => EngineScene.Load(renderer, path));
        Assert.Contains("outside every room", ex.Message);
    }

    [Fact]
    public void SceneWithoutGameplay_BakesAndAnimatesWithoutAPlayer()
    {
        using var assets = new TestAssets();
        var doc = new SceneDocument();
        doc.Add(new SceneNode { Name = "Tentacle", MeshPath = Path.Combine(SamplesDir(), "tentacle.glb"), AnimationClip = "wave" });
        string path = Bake(assets, doc);

        using var renderer = new EngineRenderer(64, 48, useWarp: true);
        using EngineScene scene = EngineScene.Load(renderer, path);
        Assert.Null(scene.Player);
        Assert.Equal("", scene.ActiveRoomName);
        renderer.SetCamera(new Vec3(0, 1.5f, 6), new Vec3(0, 1.5f, 0), 0.8f);
        renderer.Render();
        byte[] first = renderer.Pixels.ToArray();
        scene.Step(0, 0, 0.4f);   // 플레이어가 없어도 애니메이션은 진행한다
        renderer.Render();
        Assert.NotEqual(first, renderer.Pixels.ToArray());
    }

    // ---- 로더 견고성 ----
    [Fact]
    public void Loader_RejectsDamagedFilesWithAReason()
    {
        using var assets = new TestAssets();
        byte[] good = SceneBaker.BakeToBytes(PlaySamples.Load());
        using var renderer = new EngineRenderer(64, 48, useWarp: true);
        string Load(byte[] bytes)
        {
            string path = assets.Path_("damaged.ggs");
            File.WriteAllBytes(path, bytes);
            return Assert.Throws<EngineException>(() => EngineScene.Load(renderer, path)).Message;
        }

        Assert.Contains("cannot open file", Assert.Throws<EngineException>(() => EngineScene.Load(renderer, assets.Path_("nothing.ggs"))).Message);
        Assert.Contains("not a GGS1 scene file", Load(good[..10]));
        Assert.Contains("not a GGS1 scene file", Load([.. "GGM1"u8.ToArray(), .. good[4..]]));

        byte[] wrongVersion = (byte[])good.Clone();
        BitConverter.GetBytes(2u).CopyTo(wrongVersion, 4);
        Assert.Contains("unsupported scene version 2", Load(wrongVersion));

        byte[] flipped = (byte[])good.Clone();
        flipped[good.Length / 2] ^= 0x40;
        Assert.Contains("checksum mismatch", Load(flipped));
        Assert.Contains("checksum mismatch", Load(good[..^1]));                                  // 끝이 잘림
        Assert.Contains("checksum mismatch", Load([.. good, 0]));                                // 뒤에 덧붙음

        // 체크섬만 맞춰 주면(= 구조 검증이 막아야 하는 손상) 사유가 구조에 대한 것이어야 한다.
        byte[] Resealed(Action<byte[]> damage, int keep = -1)
        {
            byte[] body = good[..((keep < 0 ? good.Length : keep) - 4)];
            damage(body);
            return [.. body, .. BitConverter.GetBytes(SceneBaker.Crc32(body))];
        }
        Assert.Contains("truncated", Load(Resealed(_ => { }, good.Length / 2)));
        Assert.Contains("too many textures", Load(Resealed(b => BitConverter.GetBytes(0xFFFFFFFFu).CopyTo(b, 8))));
    }

    [Fact]
    public void Loader_SurvivesRandomDamageThatKeepsTheChecksumValid()
    {
        // 본문을 무작위로 망가뜨리고 체크섬을 다시 맞춰, 구조 검증과 엔진의 값 검증이 어떤 입력에도 죽거나 멈추지 않는지 본다.
        // 허용되는 결과는 "정상 로드" 또는 사유가 있는 EngineException뿐이다.
        using var assets = new TestAssets();
        byte[] good = SceneBaker.BakeToBytes(PlaySamples.Load());
        var random = new Random(20240607);
        using var renderer = new EngineRenderer(64, 48, useWarp: true);
        string path = assets.Path_("fuzz.ggs");
        int rejected = 0, loaded = 0;

        for (int i = 0; i < 400; i++)
        {
            byte[] body = good[..^4];
            int edits = 1 + random.Next(4);
            for (int e = 0; e < edits; e++)
            {
                int at = random.Next(8, body.Length);  // 매직/버전은 건드리지 않는다
                body[at] = random.Next(3) == 0 ? (byte)0xFF : (byte)random.Next(256);
            }
            File.WriteAllBytes(path, [.. body, .. BitConverter.GetBytes(SceneBaker.Crc32(body))]);
            try
            {
                using EngineScene scene = EngineScene.Load(renderer, path);
                scene.Step(0.5f, 0.5f, FrameSeconds);   // 로드된 뒤에도 한 프레임은 안전해야 한다
                loaded++;
            }
            catch (EngineException ex)
            {
                Assert.False(string.IsNullOrWhiteSpace(ex.Message));
                rejected++;
            }
        }
        Assert.True(rejected > 50 && loaded + rejected == 400, $"rejected {rejected}, loaded {loaded}");  // 대부분은 구조/값 검증에서 걸러진다
    }

    [Fact]
    public void DisposingTheScene_RemovesEverythingItCreated_SoTheRendererCanLoadAgain()
    {
        using var assets = new TestAssets();
        string path = Bake(assets, RichDocument(assets));
        using var renderer = new EngineRenderer(96, 72, useWarp: true);
        renderer.SetCamera(new Vec3(0, 3, 9), new Vec3(0, 0, 0), 0.8f);
        renderer.Render();
        byte[] empty = renderer.Pixels.ToArray();

        for (int i = 0; i < 25; i++)
        {
            using EngineScene scene = EngineScene.Load(renderer, path);
            scene.Step(1, 0, FrameSeconds);
        }
        renderer.SetCamera(new Vec3(0, 3, 9), new Vec3(0, 0, 0), 0.8f);
        renderer.Render();
        // 점광원 한도(8개)와 객체/리소스가 쌓이지 않았고, 씬이 설정한 조명은 남지만 오브젝트는 모두 사라져 배경만 보인다.
        Assert.Equal(empty, renderer.Pixels.ToArray());
    }

    [Fact]
    public void FailedLoad_LeavesNothingBehind()
    {
        using var assets = new TestAssets();
        SceneDocument doc = MinimalGameplayDocument(assets);
        doc.Lighting.PointLights.Add(new PointLightDef { Position = new Vec3(1, 1, 1) });
        doc.Gameplay.Player!.Spawn = new Vec2(50, 50);             // 로드 중 마지막 단계에서 실패한다
        string path = assets.Path_("late_failure.ggs");
        SceneBaker.Bake(doc, path, verify: false);

        using var renderer = new EngineRenderer(96, 72, useWarp: true);
        renderer.SetCamera(new Vec3(0, 3, 9), new Vec3(0, 0, 0), 0.8f);
        renderer.Render();
        byte[] empty = renderer.Pixels.ToArray();
        for (int i = 0; i < 12; i++)  // 점광원이 남는다면 8개 한도에서 다른 오류로 바뀐다
            Assert.Contains("outside every room", Assert.Throws<EngineException>(() => EngineScene.Load(renderer, path)).Message);
        renderer.Render();
        Assert.Equal(empty, renderer.Pixels.ToArray());            // 실패한 씬의 오브젝트가 남아 있지 않다
    }
}
