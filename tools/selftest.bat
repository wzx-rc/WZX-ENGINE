@echo off
rem Editor self-test: builds the editor, then checks the manipulators, Advanced Scale, the grid and its options, the view
rem cube, the menus inside the view, mouse selection, anti-aliasing, the selection glow and the path traced overlay, and
rem prints the PASS / FAIL report (build\selftest_report.txt). The editor window opens and drives itself for a minute:
rem leave the mouse and the keyboard alone meanwhile.
rem Usage: tools\selftest.bat [nobuild] [debug]
rem   nobuild: the editor already built;  debug: with the DirectX 12 validation layer (Windows optional feature
rem   "Graphics Tools"), its errors reported as failed checks
setlocal
cd /d "%~dp0.."
set "BUILD=1"
for %%A in (%*) do (
    if /i "%%A"=="nobuild" set "BUILD="
    if /i "%%A"=="debug" set "VP3D_DEBUG=1"
)
if defined BUILD (
    call "%CD%\build.bat" || exit /b 1
)
set "REPORT=%CD%\build\selftest_report.txt"
del /q "%REPORT%" 2>nul
start "" /wait build\bin\Viewport3D.exe --scene concept --size 1600x900 --self-test "%REPORT%"
set "CODE=%ERRORLEVEL%"
if exist "%REPORT%" (type "%REPORT%") else (echo No report: the editor did not finish the self-test. & set "CODE=1")
exit /b %CODE%
