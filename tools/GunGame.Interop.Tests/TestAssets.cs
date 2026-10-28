namespace GunGame.Interop.Tests;

/// <summary>테스트용 임시 에셋 폴더(큐브 OBJ, 체크 BMP). Dispose하면 삭제한다.</summary>
public sealed class TestAssets : IDisposable
{
    public const string CubeObj = """
        v -1 -1 1
        v 1 -1 1
        v 1 1 1
        v -1 1 1
        v -1 -1 -1
        v 1 -1 -1
        v 1 1 -1
        v -1 1 -1
        vt 0 0
        vt 1 0
        vt 1 1
        vt 0 1
        f 1/1 2/2 3/3 4/4
        f 6/1 5/2 8/3 7/4
        f 2/1 6/2 7/3 3/4
        f 5/1 1/2 4/3 8/4
        f 4/1 3/2 7/3 8/4
        f 5/1 6/2 2/3 1/4
        """;

    public string Directory { get; } = Path.Combine(Path.GetTempPath(), "gge_cs_" + Guid.NewGuid().ToString("N"));

    public TestAssets() => System.IO.Directory.CreateDirectory(Directory);

    public string Path_(string name) => Path.Combine(Directory, name);

    public string WriteCubeObj(string name = "cube.obj")
    {
        string path = Path_(name);
        File.WriteAllText(path, CubeObj);
        return path;
    }

    /// <summary>2x2 24비트 BMP: 위 행 = 빨강, 초록 / 아래 행 = 파랑, 흰색.</summary>
    public string WriteQuadTextureBmp(string name = "tex.bmp")
    {
        byte[][] rgbTopDown = [[255, 0, 0], [0, 255, 0], [0, 0, 255], [255, 255, 255]];
        const int w = 2, h = 2, rowSize = 8;
        var bytes = new byte[54 + rowSize * h];
        bytes[0] = (byte)'B'; bytes[1] = (byte)'M';
        BitConverter.GetBytes(bytes.Length).CopyTo(bytes, 2);
        BitConverter.GetBytes(54).CopyTo(bytes, 10);
        BitConverter.GetBytes(40).CopyTo(bytes, 14);
        BitConverter.GetBytes(w).CopyTo(bytes, 18);
        BitConverter.GetBytes(h).CopyTo(bytes, 22);
        bytes[26] = 1; bytes[28] = 24;
        BitConverter.GetBytes(rowSize * h).CopyTo(bytes, 34);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                byte[] rgb = rgbTopDown[y * w + x];
                int o = 54 + (h - 1 - y) * rowSize + x * 3;
                bytes[o] = rgb[2]; bytes[o + 1] = rgb[1]; bytes[o + 2] = rgb[0];
            }
        string path = Path_(name);
        File.WriteAllBytes(path, bytes);
        return path;
    }

    public void Dispose()
    {
        try { System.IO.Directory.Delete(Directory, recursive: true); } catch (IOException) { }
    }
}
