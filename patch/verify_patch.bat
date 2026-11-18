@echo off
rem Applies a patch to rom\baserom.z64 like the patch page does and checks the result.
rem Usage: verify_patch.bat [patch] [expected]   (default: the newest patch in patches\ and its build file)
cd /d "%~dp0"
if not exist rom\baserom.z64 (echo Put your ROM in rom\baserom.z64 first. & pause & exit /b 1)
set "PATCH=%~1"
if "%PATCH%"=="" for /f "delims=" %%F in (dir /b /o-d patches\*.bps 2^>nul) do if not defined PATCH set "PATCH=patches\%%F"
if "%PATCH%"=="" (echo No patch found in patches\ & pause & exit /b 1)
set "EXP=%~2"
if "%EXP%"=="" if /i "%PATCH:~-8%"=="_cia.bps" set "EXP=build\pokestadium.cia"
if "%EXP%"=="" if /i "%PATCH:~-8%"=="_elf.bps" set "EXP=build\pokestadium.elf"
echo Patch: %PATCH%
if exist "%EXP%" (py -3 tools\verify_patch.py "%PATCH%" rom\baserom.z64 "%EXP%") else (py -3 tools\verify_patch.py "%PATCH%" rom\baserom.z64)
pause
