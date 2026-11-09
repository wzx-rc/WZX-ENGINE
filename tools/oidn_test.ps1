# Open Image Denoise test of the progressive path tracer: the Cornell box rendered at 16 and 256 samples per pixel
# with the a-trous filter and with OIDN, each compared (tools\imgcmp) with a reference of 8192 samples without
# denoiser. PASS when OIDN is closer to the reference than the a-trous filter at both counts. Writes the captures and
# the comparison sheets (reference | test | difference x4) into build\oidn_test, and the report oidn_report.txt there.
# Usage: powershell -ExecutionPolicy Bypass -File tools\oidn_test.ps1 [-NoRef]   (-NoRef: keep the reference image)
param([switch]$NoRef)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'build\bin'
$out = Join-Path $root 'build\oidn_test'
New-Item -ItemType Directory -Force $out | Out-Null
$exe = Join-Path $bin 'Viewport3D.exe'
if (-not (Test-Path $exe)) { Write-Host 'build\bin\Viewport3D.exe missing: run build.bat first'; exit 1 }
if (-not (Test-Path (Join-Path $bin 'OpenImageDenoise.dll'))) { Write-Host 'OpenImageDenoise.dll missing next to the editor (build.bat fetches it)'; exit 1 }

$imgcmp = Join-Path $root 'build\imgcmp.exe'
if (-not (Test-Path $imgcmp)) {
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
    cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl /nologo /O2 /EHsc /DUNICODE /D_UNICODE `"$root\tools\imgcmp\imgcmp.cpp`" /Fe:`"$imgcmp`" /Fo:`"$root\build\\`" windowscodecs.lib ole32.lib" | Out-Null
}

function Shot($name, $spp, $denoiser) {
    $png = Join-Path $out "$name.png"
    $p = Start-Process -FilePath $exe -WorkingDirectory $bin -PassThru -ArgumentList '--scene', 'cornell', '--raster', '0',
        '--size', '1280x720', '--shot-viewport', "`"$png`"", '--spp', "$spp", '--denoiser', "$denoiser"
    if (-not $p.WaitForExit(900000)) { Stop-Process -Id $p.Id; throw "$name : timeout" }
    if (-not (Test-Path $png)) { throw "$name : no image" }
    return (Get-Content "$png.txt" | Where-Object { $_ -like 'oidn_*' }) -join ' '
}
function Rmse($test, $sheet) {
    $r = & $imgcmp (Join-Path $out 'cornell_ref.png') (Join-Path $out "$test.png") (Join-Path $out $sheet)
    if ($r -match 'rmse=([0-9.]+)') { return [double]$Matches[1] }
    throw "imgcmp: $r"
}

if (-not $NoRef -or -not (Test-Path (Join-Path $out 'cornell_ref.png'))) { Shot 'cornell_ref' 8192 0 | Out-Null }
$lines = @()
$pass = $true
foreach ($spp in 16, 256) {
    Shot "cornell_atrous_$spp" $spp 1 | Out-Null
    $info = Shot "cornell_oidn_$spp" $spp 2
    $a = Rmse "cornell_atrous_$spp" "sheet_atrous_$spp.png"
    $o = Rmse "cornell_oidn_$spp" "sheet_oidn_$spp.png"
    $ok = $o -lt $a
    if (-not $ok) { $pass = $false }
    $lines += ('{0} {1,4} spp: rmse a-trous {2,6:N3}  OIDN {3,6:N3}  ({4})' -f ($(if ($ok) { 'PASS' } else { 'FAIL' })), $spp, $a, $o, $info)
}
$lines += $(if ($pass) { 'ALL PASSED' } else { 'FAILED' })
$lines | Set-Content -Encoding utf8 (Join-Path $out 'oidn_report.txt')
$lines | ForEach-Object { Write-Host $_ }
if ($pass) { exit 0 } else { exit 1 }
