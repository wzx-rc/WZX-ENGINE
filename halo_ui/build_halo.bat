@echo off
rem Builds the standalone Halo editor UI (halo_ui\bin\HaloEditor.exe) with Dear ImGui from third_party\imgui.
rem Usage: build_halo.bat
setlocal
cd /d "%~dp0"

if defined VCToolsInstallDir goto :havevc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\halo_vsdir.txt"
set /p VSDIR=<"%TEMP%\halo_vsdir.txt"
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" (
    echo Visual Studio C++ build tools not found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
:havevc

set "IMGUI=..\third_party\imgui"
set "CFLAGS=/nologo /std:c++17 /EHsc /W3 /utf-8 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DIMGUI_DEFINE_MATH_OPERATORS /I%IMGUI% /Isrc"
if not exist obj\imgui mkdir obj\imgui
if not exist bin\assets mkdir bin\assets

if not exist obj\imgui.lib (
    echo [1/2] Dear ImGui
    cl %CFLAGS% /c %IMGUI%\imgui.cpp %IMGUI%\imgui_draw.cpp %IMGUI%\imgui_tables.cpp %IMGUI%\imgui_widgets.cpp %IMGUI%\backends\imgui_impl_win32.cpp %IMGUI%\backends\imgui_impl_dx12.cpp /Fo:obj\imgui\ || exit /b 1
    lib /nologo /OUT:obj\imgui.lib obj\imgui\*.obj || exit /b 1
)
echo [2/2] Halo editor
cl %CFLAGS% /c src\*.cpp /Fo:obj\ || exit /b 1
link /nologo /OUT:bin\HaloEditor.exe obj\*.obj obj\imgui.lib d3d12.lib dxgi.lib windowscodecs.lib user32.lib gdi32.lib ole32.lib shell32.lib /SUBSYSTEM:WINDOWS || exit /b 1
copy /Y assets\icons.png bin\assets\icons.png >nul
echo Build OK: halo_ui\bin\HaloEditor.exe
