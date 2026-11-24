@echo off
rem public.bat: the patch that goes on GitHub and the patch page.
rem Uses the normal .elf (build\normal\pokestadium.elf), the Pokethrasher defaults of cia_config\ and your ROM from rom\.
cd /d "%~dp0"
if not exist build\normal\pokestadium.elf (echo Missing build\normal\pokestadium.elf & pause & exit /b 1)
if not exist rom\baserom.z64 (echo Put your ROM in rom\baserom.z64 first. & pause & exit /b 1)
if not exist tools\flips.exe (echo Put flips.exe in tools\ first. & pause & exit /b 1)
set "VER=%~1"
if "%VER%"=="" set /p "VER=Version (for example 0.1.0): "
if "%VER%"=="" exit /b 1
set "PATCH=patches\pokestadium_v_%VER%_cia.bps"
if exist "%PATCH%" (echo %PATCH% already exists. Use another version. & pause & exit /b 1)
echo.
echo === 1/3: building the .cia (normal .elf, default icon and banner, your ROM inside) ===
copy /y build\normal\pokestadium.elf build\pokestadium.elf >nul
if not exist build\empty_user mkdir build\empty_user
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\make_cia.ps1" -UserDir "build\empty_user"
if errorlevel 1 goto fail
echo.
echo === 2/3: making the patch ===
tools\flips.exe --create --bps-delta rom\baserom.z64 build\pokestadium.cia "%PATCH%"
if errorlevel 1 goto fail
py -3 tools\check_patch.py "%PATCH%" rom\baserom.z64
if errorlevel 1 goto fail
echo.
echo === 3/3: applying the patch to your ROM like the patch page does ===
py -3 tools\verify_patch.py "%PATCH%" rom\baserom.z64 build\pokestadium.cia
if errorlevel 1 goto fail
echo.
echo ============================================================
echo  Done.
echo  Install on your 3DS:  build\verify\pokestadium_v_%VER%.cia
echo  Publish on GitHub:    %PATCH%
echo ============================================================
pause
exit /b 0
:fail
echo.
echo Something failed: copy the output above. The patch must not be published.
if exist "%PATCH%" del "%PATCH%"
pause
exit /b 1
