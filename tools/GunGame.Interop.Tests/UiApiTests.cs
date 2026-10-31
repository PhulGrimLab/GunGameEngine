using GunGame.Interop;
using GunGame.Scene;
using GunGame.Scripting;
using GunGame.ScriptHost;

namespace GunGame.Interop.Tests;

/// <summary>
/// 화면 위 글자/사각형(P3.3)의 스크립트 컨텍스트 규칙: 이름으로 가리키기, 같은 이름은 제자리 갱신, Hide로 이름 비우기, 이름 종류 충돌, 폰트 없음, 글자 한도/항목 한도,
/// MeasureText, 그리고 실제 화면 픽셀(PS1 효과를 받지 않는 정확한 색). 엔진 자체의 규칙은 engine_tests.cpp가 검증한다.
/// </summary>
public class UiApiTests
{
    // 5x7 'A'(폭 6)와 'B'(폭 5)만 있는 합성 BDF. 줄 높이 9.
    private const string TinyBdf = """
        STARTFONT 2.1
        FONTBOUNDINGBOX 6 9 0 -2
        STARTPROPERTIES 2
        FONT_ASCENT 7
        FONT_DESCENT 2
        ENDPROPERTIES
        CHARS 4
        STARTCHAR space
        ENCODING 32
        DWIDTH 4 0
        BBX 0 0 0 0
        BITMAP
        ENDCHAR
        STARTCHAR A
        ENCODING 65
        DWIDTH 6 0
        BBX 5 7 0 0
        BITMAP
        70
        88
        88
        F8
        88
        88
        88
        ENDCHAR
        STARTCHAR B
        ENCODING 66
        DWIDTH 5 0
        BBX 4 7 0 0
        BITMAP
        F0
        90
        F0
        90
        90
        90
        F0
        ENDCHAR
        STARTCHAR .notdef
        ENCODING -1
        DWIDTH 6 0
        BBX 5 7 0 0
        BITMAP
        F8
        88
        88
        88
        88
        88
        F8
        ENDCHAR
        ENDFONT
        """;

    private sealed class Rig : IDisposable
    {
        private readonly TestAssets _assets = new();
        public EngineRenderer Renderer { get; } = new(64, 48, useWarp: true);
        public EngineScene Scene { get; }
        public EngineFont? Font { get; }
        public StringWriter Log { get; } = new();

        public Rig(bool withFont = true)
        {
            string path = _assets.Path_("scene.ggs");
            SceneBaker.Bake(PlaySamples.Load(), path);
            Scene = EngineScene.Load(Renderer, path);
            Font = withFont ? EngineFont.LoadFromMemory(System.Text.Encoding.UTF8.GetBytes(TinyBdf.Replace("\r\n", "\n"))) : null;
        }

        public ScriptRunner Runner(params IGameScript[] scripts) => ScriptRunner.Create(Renderer, Scene, scripts, Log, Font);

        // 색이 정확히 rgb인 픽셀 수(BGRA).
        public int Count(uint rgb)
        {
            Renderer.Render();
            ReadOnlySpan<byte> px = Renderer.Pixels;
            int n = 0;
            for (int i = 0; i + 3 < px.Length; i += 4)
                if (px[i] == (rgb & 0xFF) && px[i + 1] == ((rgb >> 8) & 0xFF) && px[i + 2] == ((rgb >> 16) & 0xFF) && px[i + 3] == 255)
                    n++;
            return n;
        }

        // 사각형 영역(x, y, w, h) 안에서 색이 정확히 rgb인 픽셀 수.
        public int CountIn(uint rgb, int x0, int y0, int w, int h)
        {
            Renderer.Render();
            ReadOnlySpan<byte> px = Renderer.Pixels;
            int n = 0;
            for (int y = y0; y < y0 + h; y++)
                for (int x = x0; x < x0 + w; x++)
                {
                    int i = (y * Renderer.Width + x) * 4;
                    if (px[i] == (rgb & 0xFF) && px[i + 1] == ((rgb >> 8) & 0xFF) && px[i + 2] == ((rgb >> 16) & 0xFF) && px[i + 3] == 255)
                        n++;
                }
            return n;
        }

        public void Dispose()
        {
            Scene.Dispose();   // 씬은 렌더러보다 먼저
            Renderer.Dispose();
            Font?.Dispose();
            _assets.Dispose();
        }
    }

    private sealed class Lambda(Action<IScriptContext> start) : IGameScript
    {
        public void OnStart(IScriptContext context) => start(context);
    }

    [Fact]
    public void EngineFont_LoadsMeasuresAndReportsErrors()
    {
        using EngineFont font = EngineFont.LoadFromMemory(System.Text.Encoding.UTF8.GetBytes(TinyBdf));
        Assert.Equal((7, 2), font.Metrics);
        Assert.True(font.HasGlyph('A'));
        Assert.False(font.HasGlyph('Z'));
        Assert.False(font.HasGlyph(-1));
        Assert.Equal((11, 9, 1), font.Measure("AB"));
        Assert.Equal((6, 18, 2), font.Measure("A\nB"));
        Assert.Equal((0, 0, 0), font.Measure(""));
        Assert.Contains("too long", Assert.Throws<EngineException>(() => font.Measure(new string('A', 1025))).Message);
        Assert.Contains("must start with STARTFONT", Assert.Throws<EngineException>(() => EngineFont.LoadFromMemory("junk"u8)).Message);
        Assert.Contains("cannot open font file", Assert.Throws<EngineException>(() => EngineFont.Load(Path.Combine(Path.GetTempPath(), "no_such_font_file.bdf"))).Message);
        font.Dispose();
        Assert.Throws<ObjectDisposedException>(() => font.Metrics);
    }

    [Fact]
    public void Context_UiApi_ShowsUpdatesInPlaceAndHidesByName()
    {
        using var rig = new Rig();
        int finalCount = 0;
        var seen = new List<string>();
        rig.Runner(new Lambda(c =>
        {
            Assert.Equal(new TextSize(11, 9, 1), c.MeasureText("AB"));
            c.ShowRect("panel", 0, 0, 20, 12, 0x102030);
            c.ShowText("title", "AB", 1, 1, 0xFFFF00);
            seen.Add($"ui {rig.Renderer.UiCount}");
            c.ShowText("title", "A", 1, 1, 0x00FF00);                        // 같은 이름: 제자리 갱신(항목 수 그대로)
            seen.Add($"ui {rig.Renderer.UiCount}");
            c.ShowRect("panel", 0, 0, 8, 8, 0x102030, 255);                  // 사각형도 제자리 갱신
            seen.Add($"ui {rig.Renderer.UiCount}");
            seen.Add($"hide text: {c.HideText("title")} {c.HideText("title")} {c.HideText("panel")} {c.HideText("nope")} {c.HideText("")}");   // 사각형 이름을 HideText하면 false
            seen.Add($"ui {rig.Renderer.UiCount}");
            seen.Add($"hide rect: {c.HideRect("title")} {c.HideRect("panel")} {c.HideRect("panel")}");
            seen.Add($"ui {rig.Renderer.UiCount}");
            c.ShowText("again", "B", 0, 0);                                  // 지운 이름은 다시 만들 수 있다
            c.ShowRect("r2", 0, 0, 3, 3, 0x000001);
            c.ClearUi();
            seen.Add($"ui {rig.Renderer.UiCount}");
            c.ShowText("again", "B", 0, 0);
            seen.Add($"ui {rig.Renderer.UiCount}");
            finalCount = rig.Renderer.UiCount;
        })).Start();
        Assert.Equal(["ui 2", "ui 2", "ui 2", "hide text: True False False False False", "ui 1", "hide rect: False True False", "ui 0", "ui 0", "ui 1"], seen);
        Assert.Equal(1, finalCount);
    }

    [Fact]
    public void Context_UiApi_UpdatesEveryPropertyOfAnExistingItemInPlace()
    {
        using var rig = new Rig();
        rig.Runner(new Lambda(c =>
        {
            // 글자: 글자/위치/색/폭이 모두 반영되고 항목은 하나다.
            c.ShowText("t", "AB", 1, 1, 0xFFFF00);
            Assert.Equal(38, rig.CountIn(0xFFFF00, 0, 0, 20, 12));
            c.ShowText("t", "A", 30, 30, 0x00FF00);
            Assert.Equal(0, rig.CountIn(0xFFFF00, 0, 0, 64, 48));          // 옛 색/자리는 사라졌다
            Assert.Equal(18, rig.CountIn(0x00FF00, 30, 30, 10, 8));        // 새 글자/색/위치
            c.ShowText("w", "A B", 0, 20, 0xFF0000, 8);                    // 폭 8: "A"와 "B"가 두 줄(둘째 줄은 y = 29부터)
            Assert.Equal(20, rig.CountIn(0xFF0000, 0, 29, 10, 9));
            c.ShowText("w", "A B", 0, 20, 0xFF0000, 0);                    // 같은 항목의 폭만 바꾸면 한 줄로 나란히
            Assert.Equal(0, rig.CountIn(0xFF0000, 0, 29, 20, 9));
            Assert.Equal(20, rig.CountIn(0xFF0000, 10, 20, 6, 8));         // B가 같은 줄 x = 10에
            // 사각형: 크기/알파/위치/색이 반영된다.
            c.ShowRect("r", 40, 2, 6, 6, 0x0000FF);
            Assert.Equal(36, rig.CountIn(0x0000FF, 40, 2, 6, 6));
            c.ShowRect("r", 50, 10, 3, 2, 0x123456);
            Assert.Equal(0, rig.CountIn(0x0000FF, 0, 0, 64, 48));
            Assert.Equal(6, rig.CountIn(0x123456, 50, 10, 3, 2));
            Assert.Equal(3, rig.Renderer.UiCount);                         // t, w, r: 갱신은 항목을 늘리지 않았다
        })).Start();
    }

    [Fact]
    public void Context_UiApi_ReportsRealErrorsAndChangesNothing()
    {
        using var rig = new Rig();
        rig.Runner(new Lambda(c =>
        {
            c.ShowText("t", "A", 0, 0);
            c.ShowRect("r", 0, 0, 4, 4, 0x112233);
            Assert.Throws<ArgumentException>(() => c.ShowText("", "A", 0, 0));
            Assert.Throws<ArgumentException>(() => c.ShowRect(null!, 0, 0, 4, 4, 0));
            Assert.Contains("is a rectangle", Assert.Throws<InvalidOperationException>(() => c.ShowText("r", "A", 0, 0)).Message);
            Assert.Contains("is text", Assert.Throws<InvalidOperationException>(() => c.ShowRect("t", 0, 0, 4, 4, 0)).Message);
            Assert.Contains("too long", Assert.Throws<EngineException>(() => c.ShowText("t", new string('A', 1025), 5, 5, 0xFFFFFF)).Message);
            Assert.Contains("rectangle size must be positive", Assert.Throws<EngineException>(() => c.ShowRect("r", 9, 9, 0, 4, 0x445566)).Message);
            Assert.Contains("alpha must be 0..255", Assert.Throws<EngineException>(() => c.ShowRect("r", 9, 9, 4, 4, 0x445566, 300)).Message);
            Assert.Contains("too long", Assert.Throws<EngineException>(() => c.MeasureText(new string('A', 1025))).Message);
            Assert.Equal(2, rig.Renderer.UiCount);
        })).Start();
        // 실패한 갱신은 항목을 전혀 바꾸지 않았다. 사각형 r(0,0 4x4, 색 0x112233)은 글자 t보다 나중에 만들어져 글자 위에 그려지고, 글자 'A'의 켜진 픽셀 18개 중
        // 사각형 안(x,y < 4)의 9개는 가려져 흰색이 9개 남는다. 거부된 ShowRect(9,9,.., 0x445566)/ShowText(5,5,..)의 위치/색은 반영되지 않았다.
        Assert.Equal(16, rig.Count(0x112233));
        Assert.Equal(9, rig.Count(0xFFFFFF));
        Assert.Equal(0, rig.Count(0x445566));
    }

    [Fact]
    public void Context_UiApi_WithoutAFont_ThrowsForTextButNotForRectangles()
    {
        using var rig = new Rig(withFont: false);
        rig.Runner(new Lambda(c =>
        {
            Assert.Contains("no font: start the player with --font", Assert.Throws<InvalidOperationException>(() => c.ShowText("t", "A", 0, 0)).Message);
            Assert.Contains("no font", Assert.Throws<InvalidOperationException>(() => c.MeasureText("A")).Message);
            c.ShowRect("r", 0, 0, 4, 4, 0x102030);   // 사각형은 폰트가 필요 없다
            Assert.Equal(1, rig.Renderer.UiCount);
        })).Start();
    }

    [Fact]
    public void Context_UiApi_LimitIs64Items()
    {
        using var rig = new Rig();
        rig.Runner(new Lambda(c =>
        {
            for (int i = 0; i < 64; i++)
                c.ShowRect($"r{i}", 0, 0, 1, 1, 0, 0);
            Assert.Contains("too many ui items (max 64)", Assert.Throws<EngineException>(() => c.ShowRect("extra", 0, 0, 1, 1, 0)).Message);
            Assert.Contains("too many ui items", Assert.Throws<EngineException>(() => c.ShowText("extra", "A", 0, 0)).Message);
            Assert.Equal(64, rig.Renderer.UiCount);
            Assert.True(c.HideRect("r3"));
            c.ShowText("extra", "A", 0, 0);   // 자리가 나면 된다
            Assert.True(c.HideText("extra"));
        })).Start();
    }

    [Fact]
    public void UiPixels_AreExactColors_EvenWithPs1Effects()
    {
        using var rig = new Rig();
        rig.Renderer.SetRetro(vertexSnap: true, colorBits: 3, dither: true, affineTexture: true);
        rig.Runner(new Lambda(c =>
        {
            c.ShowText("t", "AB", 2, 2, 0x336699);
            c.ShowRect("r", 30, 30, 5, 4, 0x998877);
        })).Start();
        Assert.Equal(38, rig.Count(0x336699));    // A 18 + B 20 켜진 픽셀: 3비트 양자화로는 나올 수 없는 색이 그대로
        Assert.Equal(20, rig.Count(0x998877));
    }
}
