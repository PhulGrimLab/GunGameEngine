using System.Security.Cryptography;
using GunGame.Interop;

namespace GunGame.Interop.Tests;

/// <summary>
/// 저장소에 둔 실제 폰트(assets/external/galmuri)가 README에 적은 원본 그대로인지 확인한다. 폰트가 바뀌면(다른 버전/변형/수정) 이 테스트가 실패해
/// README의 표와 한글 골든을 다시 만들어야 함을 알려 준다(절차: assets/external/galmuri/README.md).
/// </summary>
public class FontFileTests
{
    private const string FontSha256 = "98716D08AA5762E6CE440D5A757D107CB5D3F32A53861878BE06C00AACC23304";
    private const string LicenseSha256 = "86A3EE9495F942F0243F18C103DA9FACA27ADB88142613EDB8BB852E56C892C1";

    private static string GalmuriDir()
    {
        for (string? dir = AppContext.BaseDirectory; dir is not null; dir = Path.GetDirectoryName(dir))
        {
            string candidate = Path.Combine(dir, "assets", "external", "galmuri");
            if (Directory.Exists(candidate))
                return candidate;
        }
        throw new DirectoryNotFoundException("assets/external/galmuri not found above the test binaries");
    }

    [Fact]
    public void GalmuriFiles_AreTheUnmodifiedOriginalsDescribedInTheReadme()
    {
        string dir = GalmuriDir();
        Assert.Equal(FontSha256, Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(dir, "Galmuri11.bdf")))));
        Assert.Equal(LicenseSha256, Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(dir, "LICENSE.txt")))));
        string readme = File.ReadAllText(Path.Combine(dir, "README.md"));
        Assert.Contains(FontSha256, readme);   // README의 표가 실제 해시와 같다
        Assert.Contains(LicenseSha256, readme);
        Assert.Contains("SIL OPEN FONT LICENSE Version 1.1", File.ReadAllText(Path.Combine(dir, "LICENSE.txt")));
    }

    [Fact]
    public void GalmuriFont_LoadsThroughTheWrapperAndMeasuresKorean()
    {
        using EngineFont font = EngineFont.Load(Path.Combine(GalmuriDir(), "Galmuri11.bdf"));
        Assert.Equal((14, 2), font.Metrics);
        Assert.True(font.HasGlyph('가') && font.HasGlyph('힣') && font.HasGlyph('A'));
        Assert.Equal((161, 16, 1), font.Measure("안녕하세요, 세계! Hello 123"));
    }
}
