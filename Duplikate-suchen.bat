@echo off
rem Sucht auf allen Festplatten (auch USB-Platten) nach doppelten Dateien.
rem Es wird nichts geloescht - nur ein Bericht im Ordner "Protokolle" erstellt.
"%~dp0DuplikateFinden.exe" %*
echo.
pause
