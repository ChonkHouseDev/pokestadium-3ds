@echo off
rem Maintainers: makes the BPS patch from the ROM to build\pokestadium.elf, then checks it carries no ROM data.
rem Usage: make_patch.bat [build file] [rom]   (defaults: build\pokestadium.elf  rom\baserom.z64)
cd /d "%~dp0"
set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=build\pokestadium.elf"
set "ROM=%~2"
if "%ROM%"=="" set "ROM=rom\baserom.z64"
if not exist "%ROM%" if exist "..\baserom.z64" set "ROM=..\baserom.z64"
set "FLIPS=tools\flips.exe"
if not exist "%FLIPS%" (echo Put flips.exe in tools\ first. & pause & exit /b 1)
if not exist "%TARGET%" (echo Build not found: %TARGET% & pause & exit /b 1)
if not exist "%ROM%" (echo ROM not found: %ROM% & pause & exit /b 1)
for %%F in ("%TARGET%") do set "OUT=patches\%%~nF%%~xF.bps"
set "OUT=%OUT:.cia.bps=_cia.bps%"
set "OUT=%OUT:.3dsx.bps=_3dsx.bps%"
set "OUT=%OUT:.elf.bps=_elf.bps%"
"%FLIPS%" --create --bps-delta "%ROM%" "%TARGET%" "%OUT%"
if errorlevel 1 (echo Flips failed. & pause & exit /b 1)
py -3 tools\check_patch.py "%OUT%" "%ROM%"
if errorlevel 1 (echo. & echo The patch is NOT clean: do not publish it. & pause & exit /b 1)
echo. & echo Patch ready: %OUT%
pause
