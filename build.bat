@echo off
rem Builds the vp3d module (static lib), its shaders and the Viewport3D demo app.
rem Usage: build.bat [debug]
setlocal
cd /d "%~dp0"

if defined VCToolsInstallDir goto :havevc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\vp3d_vsdir.txt"
set /p VSDIR=<"%TEMP%\vp3d_vsdir.txt"
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" (
    echo Visual Studio C++ build tools not found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
:havevc

set "CFG=/O2 /DNDEBUG /MD"
if /i "%1"=="debug" set "CFG=/Od /Zi /MDd /D_DEBUG"

set "OUT=build\bin"
if defined VP3D_OUT set "OUT=%VP3D_OUT%"
if not exist "%OUT%\shaders" mkdir "%OUT%\shaders"
if not exist build\obj\vp3d mkdir build\obj\vp3d
if not exist build\obj\app mkdir build\obj\app
if not exist build\lib mkdir build\lib

echo [1/3] Shaders
set "DXC=%WindowsSdkVerBinPath%x64\dxc.exe"
for %%S in (PathTrace Denoise Tonemap DDGIUpdate DDGIBlend GIShade) do (
    "%DXC%" -nologo -T cs_6_5 -E main -HV 2021 -O3 -I src\vp3d\shaders src\vp3d\shaders\%%S.hlsl -Fo "%OUT%\shaders\%%S.cso" || exit /b 1
)
for %%E in (VSMain VSShadow VSBackground) do (
    "%DXC%" -nologo -T vs_6_0 -E %%E -HV 2021 -O3 -I src\vp3d\shaders src\vp3d\shaders\Raster.hlsl -Fo "%OUT%\shaders\Raster_%%E.cso" || exit /b 1
)
for %%E in (PSMain PSGhost PSId PSBackground) do (
    "%DXC%" -nologo -T ps_6_0 -E %%E -HV 2021 -O3 -I src\vp3d\shaders src\vp3d\shaders\Raster.hlsl -Fo "%OUT%\shaders\Raster_%%E.cso" || exit /b 1
)
if exist assets xcopy /E /I /Y /Q assets "%OUT%\assets" >nul

set "CFLAGS=/nologo /std:c++17 /EHsc /W3 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN %CFG% /Isrc"

echo [2/3] vp3d module
cl %CFLAGS% /c src\vp3d\*.cpp /Fo:build\obj\vp3d\ || exit /b 1
lib /nologo /OUT:build\lib\vp3d.lib build\obj\vp3d\*.obj || exit /b 1

echo [3/3] Viewport3D app
cl %CFLAGS% /c src\app\*.cpp /Fo:build\obj\app\ || exit /b 1
link /nologo /OUT:%OUT%\Viewport3D.exe build\obj\app\*.obj build\lib\vp3d.lib d3d12.lib dxgi.lib d3d11.lib d2d1.lib dwrite.lib windowscodecs.lib user32.lib gdi32.lib ole32.lib shell32.lib shcore.lib comdlg32.lib /SUBSYSTEM:WINDOWS || exit /b 1

echo Build OK: %OUT%\Viewport3D.exe
