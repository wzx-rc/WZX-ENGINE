# DLSS Ray Reconstruction test of the progressive path tracer: the camera sways (--sway) and the view is captured mid-move
# (frame 90), with the a-trous filter and with DLSS-RR, on the Cornell box and the Concept scene. Writes the captures,
# a side-by-side sheet and dlss_report.txt (GPU time of the last frame, FPS estimate, DLSS state) into build\dlss_test.
# Usage: powershell -ExecutionPolicy Bypass -File tools\dlss_test.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'build\bin'
$out = Join-Path $root 'build\dlss_test'
New-Item -ItemType Directory -Force $out | Out-Null
$exe = Join-Path $bin 'Viewport3D.exe'
$dll = Join-Path $bin 'nvngx_dlssd.dll'
if (-not (Test-Path $dll)) { Copy-Item (Join-Path $root 'third_party\dlss\bin\nvngx_dlssd.dll') $bin }

function Shot($name, $scene, $dlss) {
    $png = Join-Path $out "$name.png"
    $p = Start-Process -FilePath $exe -WorkingDirectory $bin -PassThru -ArgumentList '--scene', $scene, '--raster', '0',
        '--size', '1280x720', '--shot-viewport', "`"$png`"", '--spp', '90', '--sway', '0.4', '--denoiser', '2', '--dlss', "$dlss"
    if (-not $p.WaitForExit(300000)) { Stop-Process -Id $p.Id; throw "$name : timeout" }
    $txt = Get-Content "$png.txt"
    $ms = [double](($txt[0] -split ' ')[0] -replace 'gpu_ms=', '')
    $d = ($txt | Where-Object { $_ -like 'spp=*' }) -replace '^spp=\S+ gpu_total_ms=\S+ ', ''
    return ('{0,-22} gpu {1,6:N2} ms (~{2,4:N0} fps GPU)  {3}' -f $name, $ms, (1000.0 / [math]::Max($ms, 0.01)), $d)
}
$lines = @()
foreach ($sc in 'cornell', 'concept') {
    $lines += Shot "${sc}_move_atrous" $sc 0
    $lines += Shot "${sc}_move_dlss" $sc 1
}
$lines | Set-Content -Encoding utf8 (Join-Path $out 'dlss_report.txt')
$lines | ForEach-Object { Write-Host $_ }

Add-Type -AssemblyName System.Drawing
$names = 'cornell_move_atrous', 'cornell_move_dlss', 'concept_move_atrous', 'concept_move_dlss'
$labels = 'Cornell, mouvement : A-trous', 'Cornell, mouvement : DLSS-RR', 'Concept, mouvement : A-trous', 'Concept, mouvement : DLSS-RR'
$first = [System.Drawing.Image]::FromFile((Join-Path $out "$($names[0]).png"))
$cw = [int]($first.Width * 0.75); $ch = [int]($first.Height * 0.75); $first.Dispose()
$bmp = New-Object System.Drawing.Bitmap ($cw * 2), (($ch + 28) * 2)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
$font = New-Object System.Drawing.Font 'Segoe UI', 13
for ($i = 0; $i -lt 4; $i++) {
    $x = ($i % 2) * $cw; $y = [math]::Floor($i / 2) * ($ch + 28)
    $im = [System.Drawing.Image]::FromFile((Join-Path $out "$($names[$i]).png"))
    $g.DrawImage($im, $x, $y + 28, $cw, $ch); $im.Dispose()
    $g.DrawString($labels[$i], $font, [System.Drawing.Brushes]::White, $x + 8, $y + 4)
}
$g.Dispose(); $bmp.Save((Join-Path $out 'dlss_mouvement.png')); $bmp.Dispose()
Write-Host "Sheet: $out\dlss_mouvement.png"
