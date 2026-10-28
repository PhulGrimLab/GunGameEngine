# WPF 뷰어 화면 검증: 앱을 --selftest로 실행해 PNG를 저장하고 물체가 그려졌는지 확인한다.
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "tools\GunGameEditor\bin\Debug\net10.0-windows\GunGameEditor.exe"
$out = Join-Path ([IO.Path]::GetTempPath()) "gge_viewer_selftest.png"
Remove-Item $out -ErrorAction SilentlyContinue

$p = Start-Process -FilePath $exe -ArgumentList "--selftest", (Join-Path $root "assets\samples\sample.scene.json"), $out -PassThru
if (-not $p.WaitForExit(30000)) { $p.Kill(); Write-Error "selftest timed out"; exit 1 }
if ($p.ExitCode -ne 0) { Write-Error "selftest exit code $($p.ExitCode)"; exit 1 }
if (-not (Test-Path $out)) { Write-Error "no output png"; exit 1 }

Add-Type -AssemblyName System.Drawing
$bmp = [Drawing.Bitmap]::FromFile($out)
try {
    $corner = $bmp.GetPixel(0, 0).ToArgb()
    $center = $bmp.GetPixel([int]($bmp.Width / 2), [int]($bmp.Height / 2)).ToArgb()
    if ($corner -eq $center) { Write-Error "viewer rendered no object (center == background)"; exit 1 }
    # 뷰어는 기본적으로 15비트 컬러(채널당 32레벨)이므로 채널 값이 허용 집합 안에 있어야 한다.
    $allowed = @{}; 0..31 | ForEach-Object { $allowed[[int][Math]::Round($_ * 255 / 31, [MidpointRounding]::AwayFromZero)] = $true }
    for ($y = 0; $y -lt $bmp.Height; $y += 3) {
        for ($x = 0; $x -lt $bmp.Width; $x += 3) {
            $c = $bmp.GetPixel($x, $y)
            foreach ($v in $c.R, $c.G, $c.B) {
                if (-not $allowed.ContainsKey([int]$v)) { Write-Error "channel value $v at ($x,$y) is not 5-bit quantized"; exit 1 }
            }
        }
    }
    Write-Host "viewer selftest OK ($($bmp.Width)x$($bmp.Height)) -> $out"
} finally { $bmp.Dispose() }
