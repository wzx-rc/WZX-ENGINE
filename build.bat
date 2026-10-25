@echo off
rem Builds the vp3d module (static lib), its shaders and the Viewport3D demo app.
rem The C++ runtime is linked statically (/MT): an exported game needs no Visual C++ redistributable.
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

set "CFG=/O2 /DNDEBUG /MT"
if /i "%1"=="debug" set "CFG=/Od /Zi /MTd /D_DEBUG"

set "OUT=build\bin"
if defined VP3D_OUT set "OUT=%VP3D_OUT%"
if not exist "%OUT%\shaders" mkdir "%OUT%\shaders"
if not exist build\obj\vp3d mkdir build\obj\vp3d
if not exist build\obj\app mkdir build\obj\app
if not exist build\lib mkdir build\lib

echo [1/6] Shaders
set "DXC=%WindowsSdkVerBinPath%x64\dxc.exe"
for %%S in (PathTrace Denoise Tonemap DDGIUpdate DDGIRaster DDGIBlend GIShade RCVisibility RCTrace RCGather RCSProbes RCSTrace RCSProbeSH RCSGather) do (
    "%DXC%" -nologo -T cs_6_5 -E main -HV 2021 -O3 -I src\vp3d\shaders src\vp3d\shaders\%%S.hlsl -Fo "%OUT%\shaders\%%S.cso" || exit /b 1
)
for %%E in (VSMain VSWire VSShadow VSBackground VSProbe) do (
    "%DXC%" -nologo -T vs_6_0 -E %%E -HV 2021 -O3 -DVP3D_DDGI_PROBES -I src\vp3d\shaders src\vp3d\shaders\Raster.hlsl -Fo "%OUT%\shaders\Raster_%%E.cso" || exit /b 1
)
for %%E in (PSMain PSGhost PSId PSWire PSBackground PSProbe PSProbeBackground) do (
    "%DXC%" -nologo -T ps_6_0 -E %%E -HV 2021 -O3 -DVP3D_DDGI_PROBES -I src\vp3d\shaders src\vp3d\shaders\Raster.hlsl -Fo "%OUT%\shaders\Raster_%%E.cso" || exit /b 1
)
if exist assets xcopy /E /I /Y /Q assets "%OUT%\assets" >nul

set "CFLAGS=/nologo /std:c++17 /EHsc /W3 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DIMGUI_DEFINE_MATH_OPERATORS %CFG% /Isrc /Ithird_party\imgui"

rem stale objects of deleted sources would be linked by the wildcards below
del /q build\obj\vp3d\*.obj build\obj\app\*.obj 2>nul

echo [2/6] vp3d module
cl %CFLAGS% /c src\vp3d\*.cpp /Fo:build\obj\vp3d\ || exit /b 1
lib /nologo /OUT:build\lib\vp3d.lib build\obj\vp3d\*.obj || exit /b 1

echo [3/6] Dear ImGui (third_party\imgui, built once)
if not exist build\obj\imgui mkdir build\obj\imgui
if not exist build\lib\imgui_mt.lib (
    cl %CFLAGS% /c third_party\imgui\imgui*.cpp third_party\imgui\backends\imgui_impl_win32.cpp third_party\imgui\backends\imgui_impl_dx12.cpp /Fo:build\obj\imgui\ || exit /b 1
    lib /nologo /OUT:build\lib\imgui_mt.lib build\obj\imgui\*.obj || exit /b 1
)

echo [4/6] Game layer (node graph, Play, builds)
if not exist build\obj\game mkdir build\obj\game
del /q build\obj\game\*.obj 2>nul
cl %CFLAGS% /c src\game\*.cpp /Fo:build\obj\game\ || exit /b 1

echo [5/6] WZX Engine editor
cl %CFLAGS% /Isrc\app\atelier /c src\app\*.cpp src\app\atelier\*.cpp /Fo:build\obj\app\ || exit /b 1
rc /nologo /fo build\obj\app\wzx.res src\app\res\wzx.rc || exit /b 1
link /nologo /OUT:%OUT%\Viewport3D.exe build\obj\app\*.obj build\obj\app\wzx.res build\obj\game\*.obj build\lib\vp3d.lib build\lib\imgui_mt.lib ws2_32.lib d3d12.lib dxgi.lib d3dcompiler.lib windowscodecs.lib user32.lib gdi32.lib ole32.lib shell32.lib shcore.lib comdlg32.lib dwmapi.lib /SUBSYSTEM:WINDOWS || exit /b 1

echo [6/6] Radiance Cascades viewer (src\rcviewer)
if not exist build\obj\rcviewer mkdir build\obj\rcviewer
del /q build\obj\rcviewer\*.obj 2>nul
cl %CFLAGS% /c src\rcviewer\*.cpp src\app\ImGuiLayer.cpp src\app\Content.cpp /Fo:build\obj\rcviewer\ || exit /b 1
link /nologo /OUT:%OUT%\RCViewer.exe build\obj\rcviewer\*.obj build\obj\app\wzx.res build\lib\vp3d.lib build\lib\imgui_mt.lib d3d12.lib dxgi.lib d3dcompiler.lib windowscodecs.lib user32.lib gdi32.lib ole32.lib shell32.lib shcore.lib comdlg32.lib /SUBSYSTEM:WINDOWS || exit /b 1

echo Build OK: %OUT%\Viewport3D.exe, %OUT%\RCViewer.exe
