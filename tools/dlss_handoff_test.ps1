# DLSS hand-off test: the camera sways until frame 60 then stops (--sway-stop); the image at frame 150 (the full-size
# accumulation and OIDN having taken over from DLSS) must match the same view rendered without DLSS (the a-trous filter
# while moving), at two resolutions. Catches stale content mixed into the new accumulation after the stop.
# PASS when imgcmp's rmse is under 4 (0-255 sRGB). Writes the captures and comparison sheets into build\dlss_handoff.
# Usage: powershell -ExecutionPolicy Bypass -File tools\dlss_handoff_test.ps1 [-Bin build\bin]
param([string]$Bin = 'build\bin')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $root $Bin
$out = Join-Path $root 'build\dlss_handoff'
New-Item -ItemType Directory -Force $out | Out-Null
$exe = Join-Path $binDir 'Viewport3D.exe'
$imgcmp = Join-Path $root 'build\imgcmp.exe'
if (-not (Test-Path $imgcmp)) {
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
    cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl /nologo /O2 /EHsc /DUNICODE /D_UNICODE `"$root\tools\imgcmp\imgcmp.cpp`" /Fe:`"$imgcmp`" /Fo:`"$root\build\\`" windowscodecs.lib ole32.lib" | Out-Null
}
function Shot($name, $size, $dlss) {
    $png = Join-Path $out "$name.png"
    $p = Start-Process -FilePath $exe -WorkingDirectory $binDir -PassThru -ArgumentList '--scene', 'concept', '--raster', '0',
        '--size', $size, '--shot-viewport', "`"$png`"", '--spp', '150', '--sway', '0.4', '--sway-stop', '60', '--denoiser', '2',
        '--dlss', "$dlss"
    if (-not $p.WaitForExit(300000)) { Stop-Process -Id $p.Id; throw "$name : timeout" }
    if (-not (Test-Path $png)) { throw "$name : no image" }
}
$pass = $true
$lines = @()
foreach ($size in '1280x720', '2400x1560') {
    Shot "stop_${size}_dlss" $size 1
    Shot "stop_${size}_nodlss" $size 0
    $r = & $imgcmp (Join-Path $out "stop_${size}_nodlss.png") (Join-Path $out "stop_${size}_dlss.png") (Join-Path $out "sheet_$size.png")
    $rmse = if ($r -match 'rmse=([0-9.]+)') { [double]$Matches[1] } else { 999 }
    $ok = $rmse -lt 4.0
    if (-not $ok) { $pass = $false }
    $lines += ('{0} {1}: after the stop, DLSS vs no DLSS rmse {2:N3}' -f ($(if ($ok) { 'PASS' } else { 'FAIL' })), $size, $rmse)
}
$lines += $(if ($pass) { 'ALL PASSED' } else { 'FAILED' })
$lines | Set-Content -Encoding utf8 (Join-Path $out 'handoff_report.txt')
$lines | ForEach-Object { Write-Host $_ }
if ($pass) { exit 0 } else { exit 1 }
