# Kopieren – Documents-Ordner auf C und Y abgleichen

Manche Programme legen ihre Dateien in `C:\Users\<Name>\Documents` ab, obwohl der
Dokumente-Ordner eigentlich auf `Y:` liegen soll. `DokumenteSync.exe` holt alles von C nach Y:

- **Fehlt eine Datei auf Y**, wird sie von C nach Y kopiert.
- **Gibt es eine Datei auf beiden Seiten**, gewinnt die mit dem **neueren Änderungsdatum**:
  Ist die Datei auf C neuer, wird Y überschrieben. Ist sie auf Y neuer, bleibt Y, wie es ist.
- **Identische Dateien** (gleiches Datum, ±2 Sekunden) werden übersprungen.
- **Gleiches Datum, aber andere Größe** → wird als *Konflikt* gemeldet und nicht angefasst.
- **C wird nie verändert, und es wird nichts gelöscht.**

Am Ende liegt auf Y alles, jeweils in der neuesten Version.

Cache-Ordner (z. B. Traktors `Coverart`, `Stripes`, `Transients` oder die
Browser-Caches von Launchern) werden übersprungen – die erzeugen die Programme
von selbst neu. Mit `--mit-caches` werden sie doch kopiert.

## Benutzung

1. Den Ordner mit den Dateien irgendwo ablegen (z. B. `Y:\Tools\Kopieren`).
2. **Zuerst `Probelauf.bat` doppelklicken.** Das zeigt nur an, was kopiert *würde*.
3. Wenn das passt: **`Sync-starten.bat` doppelklicken.**

Jeder Lauf schreibt ein Protokoll in den Unterordner `Protokolle` neben der `.exe`.

### Pfade

Standardmäßig verwendet das Programm:

| | Pfad |
|---|---|
| C | `%USERPROFILE%\Documents` (also `C:\Users\<Name>\Documents`) |
| Y | der in Windows eingestellte Dokumente-Ordner (wenn er auf Y liegt), sonst `Y:\Documents` |

Falls deine Ordner anders heißen:

```
DokumenteSync.exe --c "C:\Users\Ich\Documents" --y "Y:\Dokumente" --probelauf
```

### Optionen

| Option | Wirkung |
|---|---|
| `--probelauf` | Nur anzeigen, nichts verändern |
| `--umleiten` | Danach C durch eine Umleitung auf Y ersetzen (siehe oben) |
| `--mit-caches` | Cache-Ordner doch mitkopieren |
| `--c PFAD` / `--y PFAD` | Eigene Pfade angeben |
| `--hilfe` | Hilfe anzeigen |

## Dauerhafte Lösung: C auf Y umleiten (empfohlen)

Manche Programme (z. B. Traktor, GTA V) schreiben fest nach `C:\Users\<Name>\Documents`,
egal was in Windows eingestellt ist. Damit auch deren Dateien automatisch auf Y landen:

1. Den Kopieren-Ordner **auf Y** ablegen (nicht im Documents-Ordner auf C!).
2. **Alle Programme schließen** (Traktor, Spiele, Launcher, Office, Explorer-Fenster im Dokumente-Ordner).
3. **`Umleiten.bat` doppelklicken** und die Rückfrage mit `JA` bestätigen.

Das Programm kopiert zuerst alles nach Y und prüft, ob jede Datei angekommen ist.
Nur dann benennt es `C:\Users\<Name>\Documents` in `Documents_alt` um und legt an
seiner Stelle eine Umleitung (Junction) auf Y an. Die Programme merken davon nichts:
Sie schreiben weiter „nach C“, landen aber direkt auf Y. Nichts muss neu eingestellt werden.

Geht etwas schief (z. B. weil noch ein Programm offen ist), wird nichts verändert.
`Documents_alt` bleibt als Sicherheit liegen – wenn ein paar Tage alles normal läuft,
kannst du es löschen. Danach brauchst du das Tool nicht mehr.

## Automatisch regelmäßig ausführen (optional)

Aufgabenplanung öffnen → *Einfache Aufgabe erstellen* → z. B. „Bei Anmeldung“ →
Programm: `Y:\Tools\Kopieren\DokumenteSync.exe`.

## Gut zu wissen

- Übersprungen werden `desktop.ini`, `Thumbs.db`, Office-Sperrdateien (`~$…`)
  und Verknüpfungen/Junctions wie „Eigene Musik“ im C-Ordner.
- Kopiert wird erst in eine Temp-Datei und dann umbenannt – bricht etwas ab,
  bleibt die alte Version heil.
- Dateien, die gerade geöffnet sind (z. B. eine Outlook-`.pst`), können nicht kopiert
  werden – das Programm vorher schließen. Fehler stehen im Protokoll.
- Die Dateien auf C bleiben liegen. Löschst du auf Y etwas, das noch auf C liegt,
  kommt es beim nächsten Lauf wieder nach Y.

## Duplikate auf allen Festplatten finden

**`Duplikate-suchen.bat` doppelklicken.** `DuplikateFinden.exe` durchsucht alle
Festplatten, USB-Platten und Speicherkarten nach Dateien mit **exakt gleichem Inhalt**,
egal wie sie heißen oder wo sie liegen. **Es wird nichts gelöscht.**

- Verglichen wird zuerst die Größe, dann die ersten 64 KB, erst dann die ganze Datei.
  So müssen nur echte Kandidaten komplett gelesen werden.
- Standardmäßig nur Dateien ab 1 MB (kleine Dateien erzeugen viel Rauschen).
- Übersprungen werden Windows, Programme, ProgramData, AppData, Papierkorb,
  Junctions (z. B. die Documents-Umleitung) und OneDrive-Dateien, die nur online liegen.
- Ergebnis im Ordner `Protokolle`:
  - `Duplikate_….txt` – gut lesbar, größte Platzverschwender zuerst, neueste Datei jeweils mit `*`.
  - `Duplikate_….csv` – zum Sortieren und Filtern in Excel.

Hinweis: Nach dem Umleiten tauchen `Documents_alt` und `Y:\Documents` natürlich als
Duplikate auf – das ist die Sicherheitskopie und so gewollt.

| Option | Wirkung |
|---|---|
| `--min 100KB` | Andere Mindestgröße (z. B. `0`, `500KB`, `10MB`) |
| `--alles` | Auch Windows-, Programm- und AppData-Ordner durchsuchen |
| `--netz` | Auch Netzlaufwerke durchsuchen |
| `Y:\ E:\Fotos` | Nur diese Laufwerke/Ordner durchsuchen |

Beispiel: `DuplikateFinden.exe --min 10MB Y:\ E:\`

## Selbst kompilieren

Quellcode: `DokumenteSync.cpp` und `DuplikateFinden.cpp` (C++17, keine Abhängigkeiten).
DuplikateFinden wird genauso gebaut (bei MinGW ohne die `-l…`-Bibliotheken).

```
# Visual Studio (Developer Command Prompt)
cl /std:c++17 /EHsc /O2 /utf-8 DokumenteSync.cpp

# MinGW
g++ -std=c++17 -O2 -municode -static DokumenteSync.cpp -o DokumenteSync.exe -lshell32 -lole32 -luuid
```
