@echo off
rem Builds the FBX debug dump (tools\fbxdump.cpp) into build\fbxdump.exe.
setlocal
cd /d "%~dp0.."
if defined VCToolsInstallDir goto :havevc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\vp3d_vsdir.txt"
set /p VSDIR=<"%TEMP%\vp3d_vsdir.txt"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
:havevc
if not exist build\obj\fbxdump mkdir build\obj\fbxdump
cl /nologo /std:c++17 /EHsc /O2 /utf-8 /Isrc /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE tools\fbxdump.cpp src\app\FbxImport.cpp src\app\MeshFile.cpp src\vp3d\AssetImport.cpp src\vp3d\MeshGen.cpp /Fo:build\obj\fbxdump\ /Fe:build\fbxdump.exe ole32.lib windowscodecs.lib
