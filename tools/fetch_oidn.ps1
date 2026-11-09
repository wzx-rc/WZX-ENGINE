# Downloads Intel Open Image Denoise (Apache 2.0, prebuilt Windows x64 binaries) into third_party\oidn (git-ignored).
# Called by build.bat when third_party\oidn\bin\OpenImageDenoise.dll is missing. The editor loads the DLL at run time
# (src\vp3d\Oidn.cpp): when this download fails, the build still works and the OIDN denoiser is greyed out.
param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$version = '2.5.1'
$name = "oidn-$version.x64.windows"
$url = "https://github.com/RenderKit/oidn/releases/download/v$version/$name.zip"
$dest = Join-Path $Root 'third_party\oidn'
if (Test-Path (Join-Path $dest 'bin\OpenImageDenoise.dll')) { exit 0 }
$tmp = Join-Path $env:TEMP "wzx_oidn_$version"
try {
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    New-Item -ItemType Directory -Force $tmp | Out-Null
    $zip = Join-Path $tmp "$name.zip"
    Write-Host "OIDN $version : download ($url)"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    for ($i = 1; $i -le 3; ++$i) {
        try { Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $zip; break }
        catch { if ($i -eq 3) { throw }; Start-Sleep -Seconds (2 * $i) }
    }
    Expand-Archive -Path $zip -DestinationPath $tmp -Force
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Move-Item (Join-Path $tmp $name) $dest
    Write-Host "OIDN $version : ready in $dest"
} catch {
    Write-Host "OIDN $version : download failed ($($_.Exception.Message)); the OIDN denoiser will be unavailable"
    exit 1
} finally {
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue }
}
