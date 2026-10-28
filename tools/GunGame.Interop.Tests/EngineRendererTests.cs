using GunGame.Interop;

namespace GunGame.Interop.Tests;

/// <summary>C#에서 gge.dll을 실제로 호출하는 통합 테스트(WARP 소프트웨어 렌더).</summary>
public class EngineRendererTests
{
    private const string CubeObj = """
        v -1 -1 1
        v 1 -1 1
        v 1 1 1
        v -1 1 1
        v -1 -1 -1
        v 1 -1 -1
        v 1 1 -1
        v -1 1 -1
        f 1 2 3 4
        f 6 5 8 7
        f 2 6 7 3
        f 5 1 4 8
        f 4 3 7 8
        f 5 6 2 1
        """;

    private static string WriteTemp(string content)
    {
        string path = Path.Combine(Path.GetTempPath(), $"gge_{Guid.NewGuid():N}.obj");
        File.WriteAllText(path, content);
        return path;
    }

    private static uint PixelAt(EngineRenderer r, int x, int y) =>
        BitConverter.ToUInt32(r.Pixels.Slice(y * r.Stride + x * 4, 4));

    [Fact]
    public void Create_InvalidSize_Throws()
    {
        var ex = Assert.Throws<EngineException>(() => new EngineRenderer(0, 10, useWarp: true));
        Assert.False(string.IsNullOrEmpty(ex.Message));
    }

    [Fact]
    public void RenderObj_DrawsObjectOverBackground()
    {
        string path = WriteTemp(CubeObj);
        try
        {
            using var r = new EngineRenderer(160, 120, useWarp: true);
            r.LoadObj(path);
            r.SetCamera(new Vec3(4, 3, 5), new Vec3(0, 0, 0), 0.8f);
            r.Render();
            Assert.Equal(160 * 4 * 120, r.Pixels.Length);
            Assert.NotEqual(PixelAt(r, 0, 0), PixelAt(r, 80, 60));
        }
        finally { File.Delete(path); }
    }

    [Fact]
    public void LoadObj_MissingFile_ThrowsWithEngineMessage()
    {
        using var r = new EngineRenderer(32, 32, useWarp: true);
        var ex = Assert.Throws<EngineException>(() => r.LoadObj(@"Z:\no\such\file.obj"));
        Assert.Contains("cannot open", ex.Message);
    }

    [Fact]
    public void LoadObj_HandlesNonAsciiPath()
    {
        string dir = Path.Combine(Path.GetTempPath(), "gge_한글_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        string path = Path.Combine(dir, "큐브.obj");
        File.WriteAllText(path, CubeObj);
        try
        {
            using var r = new EngineRenderer(32, 32, useWarp: true);
            r.LoadObj(path);
        }
        finally { Directory.Delete(dir, recursive: true); }
    }

    [Fact]
    public void SetMesh_InvalidIndex_Throws()
    {
        using var r = new EngineRenderer(32, 32, useWarp: true);
        float[] v = [0, 0, 0, 0, 0, 1];
        uint[] i = [0, 1, 2];
        Assert.Throws<EngineException>(() => r.SetMesh(v, i));
    }

    [Fact]
    public void OrbitCamera_ChangesRenderedImage()
    {
        string path = WriteTemp(CubeObj);
        try
        {
            using var r = new EngineRenderer(96, 72, useWarp: true);
            r.LoadObj(path);
            var cam = new OrbitCamera();
            r.SetCamera(cam.Eye, cam.Target, 0.8f);
            r.Render();
            byte[] before = r.Pixels.ToArray();
            cam.Rotate(1.0f, 0);
            r.SetCamera(cam.Eye, cam.Target, 0.8f);
            r.Render();
            Assert.NotEqual(before, r.Pixels.ToArray());
        }
        finally { File.Delete(path); }
    }

    [Fact]
    public void Dispose_IsIdempotent_AndUseAfterDisposeThrows()
    {
        var r = new EngineRenderer(16, 16, useWarp: true);
        r.Dispose();
        r.Dispose();
        Assert.Throws<ObjectDisposedException>(() => r.Render());
    }
}
