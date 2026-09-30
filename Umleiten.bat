@echo off
rem Kopiert alles nach Y und ersetzt danach den Documents-Ordner auf C
rem durch eine Umleitung (Junction) auf Y. Vorher alle Programme schliessen!
"%~dp0DokumenteSync.exe" --umleiten %*
echo.
pause
