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
| `--c PFAD` / `--y PFAD` | Eigene Pfade angeben |
| `--hilfe` | Hilfe anzeigen |

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

## Selbst kompilieren

Quellcode: `DokumenteSync.cpp` (C++17, keine Abhängigkeiten).

```
# Visual Studio (Developer Command Prompt)
cl /std:c++17 /EHsc /O2 /utf-8 DokumenteSync.cpp

# MinGW
g++ -std=c++17 -O2 -municode -static DokumenteSync.cpp -o DokumenteSync.exe -lshell32 -lole32 -luuid
```
