using GunGame.Interop;

namespace GunGame.Interop.Tests;

/// <summary>C#에서 gge.dll을 실제로 호출하는 통합 테스트(WARP 소프트웨어 렌더).</summary>
public class EngineRendererTests
{
    private static uint PixelAt(EngineRenderer r, int x, int y) =>
        BitConverter.ToUInt32(r.Pixels.Slice(y * r.Stride + x * 4, 4));

    private static EngineRenderer NewCubeRenderer(TestAssets assets, int width = 160, int height = 120)
    {
        var r = new EngineRenderer(width, height, useWarp: true);
        int mesh = r.LoadMeshObj(assets.WriteCubeObj());
        r.CreateObject(mesh);
        r.SetCamera(new Vec3(4, 3, 5), new Vec3(0, 0, 0), 0.8f);
        return r;
    }

    [Fact]
    public void Create_InvalidSize_Throws()
    {
        var ex = Assert.Throws<EngineException>(() => new EngineRenderer(0, 10, useWarp: true));
        Assert.False(string.IsNullOrEmpty(ex.Message));
    }

    [Fact]
    public void RenderObj_DrawsObjectOverBackground()
    {
        using var assets = new TestAssets();
        using var r = NewCubeRenderer(assets);
        r.Render();
        Assert.Equal(160 * 4 * 120, r.Pixels.Length);
        Assert.NotEqual(PixelAt(r, 0, 0), PixelAt(r, 80, 60));
    }

    [Fact]
    public void LoadMeshObj_MissingFile_ThrowsWithEngineMessage()
    {
        using var r = new EngineRenderer(32, 32, useWarp: true);
        var ex = Assert.Throws<EngineException>(() => r.LoadMeshObj(@"Z:\no\such\file.obj"));
        Assert.Contains("cannot open", ex.Message);
    }

    [Fact]
    public void LoadMeshObj_HandlesNonAsciiPath()
    {
        using var assets = new TestAssets();
        string dir = Path.Combine(assets.Directory, "한글");
        Directory.CreateDirectory(dir);
        string path = Path.Combine(dir, "큐브.obj");
        File.WriteAllText(path, TestAssets.CubeObj);
        using var r = new EngineRenderer(32, 32, useWarp: true);
        Assert.True(r.LoadMeshObj(path) > 0);
    }

    [Fact]
    public void CreateMesh_InvalidIndex_Throws()
    {
        using var r = new EngineRenderer(32, 32, useWarp: true);
        float[] v = [0, 0, 0, 0, 0, 1, 0, 0];
        uint[] i = [0, 1, 2];
        Assert.Throws<EngineException>(() => r.CreateMesh(v, i));
    }

    [Fact]
    public void OrbitCamera_ChangesRenderedImage()
    {
        using var assets = new TestAssets();
        using var r = NewCubeRenderer(assets, 96, 72);
        var cam = new OrbitCamera();
        r.SetCamera(cam.Eye, cam.Target, 0.8f);
        r.Render();
        byte[] before = r.Pixels.ToArray();
        cam.Rotate(1.0f, 0);
        r.SetCamera(cam.Eye, cam.Target, 0.8f);
        r.Render();
        Assert.NotEqual(before, r.Pixels.ToArray());
    }

    [Fact]
    public void SetRetro_InvalidBits_Throws()
    {
        using var r = new EngineRenderer(16, 16, useWarp: true);
        Assert.Throws<EngineException>(() => r.SetRetro(false, 0, false));
        Assert.Throws<EngineException>(() => r.SetRetro(false, 9, false));
        r.SetRetro(true, 5, true, affineTexture: true);
    }

    [Fact]
    public void SetRetro_OneBit_ProducesOnlyBlackOrFullChannels()
    {
        using var assets = new TestAssets();
        using var r = NewCubeRenderer(assets, 96, 72);
        r.SetRetro(vertexSnap: false, colorBits: 1, dither: false);
        r.Render();
        ReadOnlySpan<byte> px = r.Pixels;
        for (int i = 0; i < px.Length; i += 4)
            for (int c = 0; c < 3; c++)
                Assert.True(px[i + c] is 0 or 255, $"channel value {px[i + c]} at byte {i + c}");
    }

    [Fact]
    public void Texture_FromFile_AppearsUprightOnQuad()
    {
        using var assets = new TestAssets();
        using var r = new EngineRenderer(160, 120, useWarp: true);
        float[] quad =
        [
            -1, -1, 0, 0, 0, 1, 0, 1,
             1, -1, 0, 0, 0, 1, 1, 1,
             1,  1, 0, 0, 0, 1, 1, 0,
            -1,  1, 0, 0, 0, 1, 0, 0,
        ];
        int mesh = r.CreateMesh(quad, [0, 1, 2, 0, 2, 3]);
        int tex = r.LoadTexture(assets.WriteQuadTextureBmp());
        int obj = r.CreateObject(mesh, tex);
        r.SetObjectColor(obj, new Vec3(1, 1, 1));
        r.SetCamera(new Vec3(0, 0, 4), new Vec3(0, 0, 0), 0.8f);
        r.Render();

        uint topLeft = PixelAt(r, 62, 42), bottomRight = PixelAt(r, 98, 78);
        Assert.True(((topLeft >> 16) & 0xFF) > 100 && (topLeft & 0xFF) < 20);  // 빨강
        Assert.True((bottomRight & 0xFF) > 100 && ((bottomRight >> 8) & 0xFF) > 100);  // 흰색
    }

    [Fact]
    public void Texture_InvalidArguments_Throw()
    {
        using var assets = new TestAssets();
        using var r = new EngineRenderer(16, 16, useWarp: true);
        Assert.Throws<EngineException>(() => r.CreateTexture(new byte[16], 0, 2));
        Assert.Throws<EngineException>(() => r.LoadTexture(assets.Path_("missing.png")));
    }

    [Fact]
    public void ConvertObjToGgm_RendersIdenticallyToObj()
    {
        using var assets = new TestAssets();
        string obj = assets.WriteCubeObj();
        string ggm = assets.Path_("cube.ggm");
        EngineRenderer.ConvertObjToGgm(obj, ggm);

        byte[] Render(Func<EngineRenderer, int> loadMesh)
        {
            using var r = new EngineRenderer(96, 72, useWarp: true);
            r.CreateObject(loadMesh(r));
            r.SetCamera(new Vec3(4, 3, 5), new Vec3(0, 0, 0), 0.8f);
            r.Render();
            return r.Pixels.ToArray();
        }
        Assert.Equal(Render(r => r.LoadMeshObj(obj)), Render(r => r.LoadMeshGgm(ggm)));
        Assert.Throws<EngineException>(() => EngineRenderer.ConvertObjToGgm(assets.Path_("missing.obj"), ggm));
    }

    [Fact]
    public void SetObjectParent_RejectsCycle()
    {
        using var assets = new TestAssets();
        using var r = new EngineRenderer(16, 16, useWarp: true);
        int mesh = r.LoadMeshObj(assets.WriteCubeObj());
        int a = r.CreateObject(mesh);
        int b = r.CreateObject(mesh, 0, a);
        var ex = Assert.Throws<EngineException>(() => r.SetObjectParent(a, b));
        Assert.Contains("cycle", ex.Message);
        r.SetObjectParent(b, 0);
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

public class EngineRendererArgumentTests
{
    // 네이티브 쪽은 배열 길이를 알 수 없으므로 짧은 배열을 그대로 넘기면 pinned 배열 뒤쪽 메모리를 읽는다.
    // 래퍼가 ArgumentException으로 막아야 한다. (과다 읽기가 작은 크기만 쓴다.)
    [Fact]
    public void CreateTexture_ShortBuffer_IsRejected()
    {
        using var r = new EngineRenderer(16, 16, useWarp: true);
        Assert.Throws<ArgumentException>(() => r.CreateTexture(new byte[16], 8, 8));  // 8x8x4 = 256바이트가 필요
        Assert.True(r.CreateTexture(new byte[256], 8, 8) > 0);
        Assert.Throws<EngineException>(() => r.CreateTexture(new byte[16], 0, 8));    // 크기 자체의 오류는 기존처럼 엔진이 설명한다
    }

    [Fact]
    public void CreateSkinnedMesh_ShortJointsOrWeights_AreRejected()
    {
        using var r = new EngineRenderer(16, 16, useWarp: true);
        float[] vertices = new float[3 * 8];
        uint[] indices = [0, 1, 2];
        Assert.Throws<ArgumentException>(() => r.CreateSkinnedMesh(vertices, new uint[4], new float[12], indices));
        Assert.Throws<ArgumentException>(() => r.CreateSkinnedMesh(vertices, new uint[12], new float[4], indices));
        Assert.True(r.CreateSkinnedMesh(vertices, new uint[12], new float[12], indices) > 0);
    }

    [Fact]
    public void CreateSkeleton_ShortArrays_AreRejected()
    {
        using var r = new EngineRenderer(16, 16, useWarp: true);
        SkeletonData Make(int ibm = 48, int t = 9, int rot = 12, int s = 9)
        {
            var q = new float[rot];
            for (int i = 3; i < rot; i += 4) q[i] = 1;
            return new SkeletonData([-1, 0, 1], new float[ibm], new float[t], q, Enumerable.Repeat(1f, s).ToArray());
        }
        Assert.Throws<ArgumentException>(() => r.CreateSkeleton(Make(ibm: 16)));
        Assert.Throws<ArgumentException>(() => r.CreateSkeleton(Make(t: 3)));
        Assert.Throws<ArgumentException>(() => r.CreateSkeleton(Make(rot: 4)));
        Assert.Throws<ArgumentException>(() => r.CreateSkeleton(Make(s: 3)));
    }

    [Fact]
    public void CreateClip_ValuesShorterThanKeys_AreRejected()
    {
        using var r = new EngineRenderer(16, 16, useWarp: true);
        float[] times = [0f, 1f];
        var shortRotation = new ClipData("c", 1f, [new ClipChannelData(0, ChannelPath.Rotation, times, new float[4])]);   // 키 2개 x 4
        var shortTranslation = new ClipData("c", 1f, [new ClipChannelData(0, ChannelPath.Translation, times, new float[3])]);  // 키 2개 x 3
        Assert.Throws<ArgumentException>(() => r.CreateClip(shortRotation));
        Assert.Throws<ArgumentException>(() => r.CreateClip(shortTranslation));
    }
}
