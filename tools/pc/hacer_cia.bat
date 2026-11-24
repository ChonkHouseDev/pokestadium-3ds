@echo off
rem Arma pokestadium.cia con lo que esta en esta carpeta (ver hacer_cia.ps1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0hacer_cia.ps1"
if errorlevel 1 (echo. & echo Algo fallo: copia todo lo de arriba y mandamelo.)
pause
