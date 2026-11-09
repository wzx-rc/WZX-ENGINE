# Downloads the parts of the NVIDIA DLSS SDK (NGX, github.com/NVIDIA/DLSS) that the editor needs for DLSS Ray
# Reconstruction into third_party\dlss (git-ignored): the headers, the static NGX library (/MT) and nvngx_dlssd.dll.
# Called by build.bat when third_party\dlss\lib\nvsdk_ngx_s.lib is missing. Without them the editor builds without
# DLSS (the option is greyed out in the Render tab). The DLSS SDK is under the NVIDIA RTX SDKs license (LICENSE.txt,
# downloaded with it): nvngx_dlssd.dll may be shipped with an application.
param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$version = '310.9.16'
$base = "https://raw.githubusercontent.com/NVIDIA/DLSS/v$version"
$dest = Join-Path $Root 'third_party\dlss'
if (Test-Path (Join-Path $dest 'lib\nvsdk_ngx_s.lib')) { exit 0 }
$headers = 'nvsdk_ngx.h', 'nvsdk_ngx_defs.h', 'nvsdk_ngx_defs_dlssd.h', 'nvsdk_ngx_helpers.h', 'nvsdk_ngx_helpers_dlssd.h',
           'nvsdk_ngx_helpers_dlssd_d3d.h', 'nvsdk_ngx_helpers_d3d.h', 'nvsdk_ngx_helpers_cuda.h', 'nvsdk_ngx_params.h', 'nvsdk_ngx_params_dlssd.h'
$files = @{}
foreach ($h in $headers) { $files["include/$h"] = "include\$h" }
$files['lib/Windows_x86_64/x64/nvsdk_ngx_s.lib'] = 'lib\nvsdk_ngx_s.lib'
$files['lib/Windows_x86_64/rel/nvngx_dlssd.dll'] = 'bin\nvngx_dlssd.dll'
$files['LICENSE.txt'] = 'LICENSE.txt'
$tmp = Join-Path $env:TEMP "wzx_dlss_$version"
try {
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Write-Host "DLSS SDK $version : download (github.com/NVIDIA/DLSS)"
    foreach ($k in $files.Keys) {
        $to = Join-Path $tmp $files[$k]
        New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
        for ($i = 1; $i -le 3; ++$i) {
            try { Invoke-WebRequest -UseBasicParsing -Uri "$base/$k" -OutFile $to; break }
            catch { if ($i -eq 3) { throw }; Start-Sleep -Seconds (2 * $i) }
        }
    }
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Move-Item $tmp $dest
    Write-Host "DLSS SDK $version : ready in $dest"
} catch {
    Write-Host "DLSS SDK $version : download failed ($($_.Exception.Message)); DLSS Ray Reconstruction will be unavailable"
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue }
    exit 1
}
