<#
.SYNOPSIS
    Gleicht zwei Dokumente-Ordner (z. B. C: und Y:) ab. Bei doppelten Dateien
    gewinnt immer die Version mit dem neueren Aenderungsdatum.

.DESCRIPTION
    Das Skript arbeitet in zwei Durchgaengen mit robocopy (in Windows eingebaut):

      1. C -> Y : Dateien, die auf Y fehlen oder auf C neuer sind, werden nach Y kopiert.
      2. Y -> C : Dateien, die auf C fehlen oder auf Y neuer sind, werden nach C kopiert.

    Danach liegen in beiden Ordnern alle Dateien, jeweils in der neuesten Version.
    Identische Dateien (gleiches Datum, gleiche Groesse) werden uebersprungen.
    Es wird NIEMALS etwas geloescht. Eine aeltere Version wird nur dann
    ueberschrieben, wenn es auf der anderen Seite eine neuere gibt.

    Mit -NurNachY wird nur Durchgang 1 ausgefuehrt (alles landet auf Y, C bleibt
    unveraendert).

.PARAMETER OrdnerC
    Documents-Ordner auf C. Standard: C:\Users\<Name>\Documents

.PARAMETER OrdnerY
    Documents-Ordner auf Y. Standard: der in Windows eingestellte Dokumente-Ordner,
    falls er auf Y liegt, sonst Y:\Documents

.PARAMETER Probelauf
    Zeigt nur an, was kopiert WUERDE, ohne etwas zu veraendern.

.PARAMETER NurNachY
    Nur in eine Richtung abgleichen: C -> Y.

.EXAMPLE
    .\Dokumente-Sync.ps1 -Probelauf
.EXAMPLE
    .\Dokumente-Sync.ps1
.EXAMPLE
    .\Dokumente-Sync.ps1 -OrdnerY "Y:\Eigene Dateien" -NurNachY
#>
[CmdletBinding()]
param(
    [string]$OrdnerC,
    [string]$OrdnerY,
    [switch]$Probelauf,
    [switch]$NurNachY
)

$ErrorActionPreference = 'Stop'

# --- Standardpfade ermitteln -------------------------------------------------
if (-not $OrdnerC) {
    $OrdnerC = Join-Path $env:USERPROFILE 'Documents'
}
if (-not $OrdnerY) {
    $eingestellt = [Environment]::GetFolderPath('MyDocuments')
    if ($eingestellt -and $eingestellt.ToUpper().StartsWith('Y:')) {
        $OrdnerY = $eingestellt
    } else {
        $OrdnerY = 'Y:\Documents'
    }
}

$OrdnerC = $OrdnerC.TrimEnd('\')
$OrdnerY = $OrdnerY.TrimEnd('\')

Write-Host ''
Write-Host "Ordner C : $OrdnerC"
Write-Host "Ordner Y : $OrdnerY"
Write-Host ''

# --- Sicherheitspruefungen ---------------------------------------------------
if (-not (Test-Path -LiteralPath $OrdnerC -PathType Container)) {
    throw "Ordner C wurde nicht gefunden: $OrdnerC"
}
if (-not (Test-Path -LiteralPath $OrdnerY -PathType Container)) {
    throw "Ordner Y wurde nicht gefunden: $OrdnerY  (Laufwerk Y angeschlossen? Pfad mit -OrdnerY angeben.)"
}
$echtC = (Get-Item -LiteralPath $OrdnerC).FullName.TrimEnd('\')
$echtY = (Get-Item -LiteralPath $OrdnerY).FullName.TrimEnd('\')
if ($echtC -ieq $echtY) {
    throw 'Ordner C und Ordner Y sind derselbe Ordner - es gibt nichts abzugleichen.'
}
if ($echtY.StartsWith("$echtC\", 'OrdinalIgnoreCase') -or $echtC.StartsWith("$echtY\", 'OrdinalIgnoreCase')) {
    throw 'Ein Ordner liegt innerhalb des anderen - das wuerde eine Endlosschleife erzeugen.'
}

# --- Protokoll ---------------------------------------------------------------
$logOrdner = Join-Path $PSScriptRoot 'Protokolle'
New-Item -ItemType Directory -Path $logOrdner -Force | Out-Null
$zeit = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$logDatei = Join-Path $logOrdner "Sync_$zeit.log"

# --- robocopy-Optionen -------------------------------------------------------
#   /E        Unterordner inkl. leerer Ordner
#   /XO       aeltere Dateien NICHT kopieren -> nur neuere oder fehlende Dateien
#   /XJ       Verknuepfungen (Junctions) wie "Eigene Musik" ueberspringen
#   /COPY:DAT Daten, Attribute, Zeitstempel  /DCOPY:T Ordner-Zeitstempel
#   /R:1 /W:1 bei gesperrten Dateien nur 1x nach 1 Sekunde erneut versuchen
#   /XF       Windows-Systemdateien des Ordners nicht anfassen
#   /FFT      2-Sekunden-Toleranz beim Datumsvergleich (wichtig, falls Y
#             FAT32/exFAT ist - sonst wuerde immer wieder hin und her kopiert)
$optionen = @(
    '/E', '/XO', '/XJ', '/FFT',
    '/COPY:DAT', '/DCOPY:T',
    '/R:1', '/W:1',
    '/XF', 'desktop.ini', 'Thumbs.db', '~$*',
    '/NP', '/NDL', '/TEE', "/UNILOG+:$logDatei"
)
if ($Probelauf) {
    $optionen += '/L'
    Write-Host '*** PROBELAUF: Es wird nichts kopiert, nur angezeigt. ***' -ForegroundColor Yellow
    Write-Host ''
}

function Start-Durchgang([string]$von, [string]$nach, [string]$titel) {
    Write-Host "=== $titel ===" -ForegroundColor Cyan
    Write-Host "    $von  ->  $nach"
    # Out-Host, damit die robocopy-Ausgabe nicht im Rueckgabewert landet
    & robocopy.exe $von $nach @optionen | Out-Host
    $code = $LASTEXITCODE
    # robocopy: 0-7 = Erfolg, ab 8 = mindestens ein Fehler
    if ($code -ge 8) {
        Write-Host "Achtung: Beim Durchgang '$titel' gab es Fehler (Code $code). Siehe Protokoll." -ForegroundColor Red
    } elseif ($code -eq 0) {
        Write-Host 'Nichts zu tun - alles schon aktuell.' -ForegroundColor Green
    } else {
        Write-Host 'Fertig.' -ForegroundColor Green
    }
    Write-Host ''
    return $code
}

$code1 = Start-Durchgang $OrdnerC $OrdnerY 'Durchgang 1: C -> Y'
$code2 = 0
if (-not $NurNachY) {
    $code2 = Start-Durchgang $OrdnerY $OrdnerC 'Durchgang 2: Y -> C'
}

Write-Host "Protokoll: $logDatei"
if ($code1 -ge 8 -or $code2 -ge 8) {
    exit 1
}
exit 0
