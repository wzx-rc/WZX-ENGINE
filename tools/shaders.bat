@echo off
rem Recompiles the shaders only (into build/bin/shaders), for quick shader iterations. Run from anywhere.
rem (the same list as build.bat)
cd /d "%~dp0.."
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSDIR=%%i
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
set "OUT=build\bin"
set "DXC=%WindowsSdkVerBinPath%x64\dxc.exe"
for %%S in (PathTrace Denoise Tonemap DDGIUpdate DDGIRaster DDGIBlend GIShade) do (
    "%DXC%" -nologo -T cs_6_5 -E main -HV 2021 -O3 -I src\vp3d\shaders src\vp3d\shaders\%%S.hlsl -Fo "%OUT%\shaders\%%S.cso" || exit /b 1
)
for %%E in (VSMain VSWire VSShadow VSBackground VSProbe) do (
    "%DXC%" -nologo -T vs_6_0 -E %%E -HV 2021 -O3 -DVP3D_DDGI_PROBES -I src\vp3d\shaders src\vp3d\shaders\Raster.hlsl -Fo "%OUT%\shaders\Raster_%%E.cso" || exit /b 1
)
for %%E in (PSMain PSGhost PSId PSSelect PSWire PSBackground PSProbe PSProbeBackground) do (
    "%DXC%" -nologo -T ps_6_0 -E %%E -HV 2021 -O3 -DVP3D_DDGI_PROBES -I src\vp3d\shaders src\vp3d\shaders\Raster.hlsl -Fo "%OUT%\shaders\Raster_%%E.cso" || exit /b 1
)
"%DXC%" -nologo -T vs_6_0 -E VSGrid -HV 2021 -O3 src\vp3d\shaders\EditorGrid.hlsl -Fo "%OUT%\shaders\EditorGrid_VSGrid.cso" || exit /b 1
"%DXC%" -nologo -T ps_6_0 -E PSGrid -HV 2021 -O3 src\vp3d\shaders\EditorGrid.hlsl -Fo "%OUT%\shaders\EditorGrid_PSGrid.cso" || exit /b 1
echo shaders OK
