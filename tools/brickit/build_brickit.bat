@echo off
rem Fast rebuild of BrickIt.exe alone (vp3d.lib and imgui already built by build.bat). Usage: tools\brickit\build_brickit.bat [vp3d]
rem   vp3d: also recompiles the vp3d module and its shaders (full build.bat otherwise)
setlocal
cd /d "%~dp0..\.."
if /i "%1"=="vp3d" ( call build.bat & exit /b %ERRORLEVEL% )
if defined VCToolsInstallDir goto :havevc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\vp3d_vsdir.txt"
set /p VSDIR=<"%TEMP%\vp3d_vsdir.txt"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
:havevc
set "OUT=build\bin"
set "CFLAGS=/nologo /std:c++17 /EHsc /W3 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DIMGUI_DEFINE_MATH_OPERATORS /O2 /DNDEBUG /MT /Isrc /Ithird_party\imgui"
if not exist build\obj\brickit mkdir build\obj\brickit
if not exist build\obj\brickit\Theme.obj cl %CFLAGS% /c src\app\ImGuiLayer.cpp src\app\Theme.cpp /Fo:build\obj\brickit\ || exit /b 1
cl %CFLAGS% /c src\brickit\*.cpp /Fo:build\obj\brickit\ || exit /b 1
link /nologo /OUT:%OUT%\BrickIt.exe build\obj\brickit\*.obj build\obj\app\wzx.res build\lib\vp3d.lib build\lib\imgui_te_mt.lib d3d12.lib dxgi.lib d3dcompiler.lib windowscodecs.lib user32.lib gdi32.lib ole32.lib shell32.lib shcore.lib comdlg32.lib dwmapi.lib /SUBSYSTEM:WINDOWS || exit /b 1
if exist assets\brickit xcopy /E /I /Y /Q /D assets\brickit "%OUT%\assets\brickit" >nul
if exist assets\hdri xcopy /E /I /Y /Q /D assets\hdri "%OUT%\assets\hdri" >nul
if not exist "%OUT%\assets\brickit\technic" mkdir "%OUT%\assets\brickit\technic"
xcopy /Y /Q /D docs\brickit\web\technic\g*.wasm "%OUT%\assets\brickit\technic\" >nul
xcopy /Y /Q /D docs\brickit\web\technic\index.json "%OUT%\assets\brickit\technic\" >nul
echo Build OK: %OUT%\BrickIt.exe
