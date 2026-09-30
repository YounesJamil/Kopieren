@echo off
rem Doppelklick: gleicht C:\...\Documents und den Dokumente-Ordner auf Y ab.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Dokumente-Sync.ps1" %*
echo.
pause
