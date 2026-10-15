@echo off
rem Builds WzxShaderGraph.exe (Maya shader graph prototype) into build\mayashader.
rem Needs the engine libraries and shaders: run build.bat once first.
setlocal
cd /d "%~dp0\..\.."

if defined VCToolsInstallDir goto :havevc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\vp3d_vsdir.txt"
set /p VSDIR=<"%TEMP%\vp3d_vsdir.txt"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
:havevc

if not exist build\lib\vp3d.lib (echo Run build.bat first & exit /b 1)
set "OUT=build\mayashader"
if not exist %OUT%\shaders mkdir %OUT%\shaders
if not exist build\obj\mayashader mkdir build\obj\mayashader
xcopy /Y /Q build\bin\shaders\*.cso %OUT%\shaders\ >nul
if exist assets\themes xcopy /E /I /Y /Q assets\themes %OUT%\assets\themes >nul

set "CFLAGS=/nologo /std:c++17 /EHsc /W3 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DIMGUI_DEFINE_MATH_OPERATORS /O2 /DNDEBUG /MT /Isrc /Ithird_party\imgui"
del /q build\obj\mayashader\*.obj 2>nul
cl %CFLAGS% /c src\mayashader\*.cpp src\game\Graph.cpp src\game\NodeEditor.cpp src\game\Runtime.cpp src\app\ImGuiLayer.cpp src\app\Theme.cpp /Fo:build\obj\mayashader\ || exit /b 1
link /nologo /OUT:%OUT%\WzxShaderGraph.exe build\obj\mayashader\*.obj build\lib\vp3d.lib build\lib\imgui_mt.lib ws2_32.lib d3d12.lib dxgi.lib d3dcompiler.lib windowscodecs.lib user32.lib gdi32.lib ole32.lib shell32.lib shcore.lib comdlg32.lib dwmapi.lib /SUBSYSTEM:WINDOWS || exit /b 1
echo Build OK: %OUT%\WzxShaderGraph.exe
