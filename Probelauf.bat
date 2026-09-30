@echo off
rem Doppelklick: zeigt nur an, was kopiert wuerde - veraendert nichts.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Dokumente-Sync.ps1" -Probelauf %*
echo.
pause
