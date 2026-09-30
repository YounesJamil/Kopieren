# Kopieren – Documents-Ordner auf C und Y abgleichen

Manche Programme legen ihre Dateien in `C:\Users\<Name>\Documents` ab, obwohl der
Dokumente-Ordner eigentlich auf `Y:` liegen soll. Dieses Skript gleicht beide Ordner ab:

- **Fehlt eine Datei auf einer Seite**, wird sie dorthin kopiert.
- **Gibt es eine Datei auf beiden Seiten**, gewinnt die mit dem **neueren Änderungsdatum**.
  Die ältere Version wird überschrieben.
- **Identische Dateien** (gleiches Datum, gleiche Größe) werden übersprungen.
- **Es wird nichts gelöscht.**

Am Ende liegen in beiden Ordnern alle Dateien, jeweils in der neuesten Version.

## Benutzung

1. Den Ordner mit den Dateien irgendwo ablegen (z. B. `Y:\Tools\Kopieren`).
2. **Zuerst `Probelauf.bat` doppelklicken.** Das zeigt nur an, was kopiert *würde*.
3. Wenn das passt: **`Sync-starten.bat` doppelklicken.**

Jeder Lauf schreibt ein Protokoll in den Unterordner `Protokolle`.

### Pfade

Standardmäßig verwendet das Skript:

| | Pfad |
|---|---|
| C | `C:\Users\<Name>\Documents` |
| Y | der in Windows eingestellte Dokumente-Ordner (wenn er auf Y liegt), sonst `Y:\Documents` |

Falls deine Ordner anders heißen, in PowerShell aufrufen:

```powershell
.\Dokumente-Sync.ps1 -OrdnerC "C:\Users\Ich\Documents" -OrdnerY "Y:\Dokumente" -Probelauf
```

### Optionen

| Option | Wirkung |
|---|---|
| `-Probelauf` | Nur anzeigen, nichts verändern |
| `-NurNachY` | Nur C → Y kopieren, C bleibt unverändert |
| `-OrdnerC` / `-OrdnerY` | Eigene Pfade angeben |

## Automatisch regelmäßig ausführen (optional)

Aufgabenplanung öffnen → *Einfache Aufgabe erstellen* → z. B. „Bei Anmeldung“ →
Programm: `powershell.exe`, Argumente:

```
-NoProfile -ExecutionPolicy Bypass -File "Y:\Tools\Kopieren\Dokumente-Sync.ps1"
```

## Gut zu wissen

- Übersprungen werden `desktop.ini`, `Thumbs.db` und Office-Sperrdateien (`~$…`),
  außerdem die versteckten Verknüpfungen wie „Eigene Musik“ im C-Ordner.
- Dateien, die gerade geöffnet sind (z. B. eine Outlook-`.pst`), können nicht kopiert
  werden – das Programm vorher schließen. Fehler stehen im Protokoll.
- Gelöschte Dateien werden **nicht** mitgelöscht: Wer auf einer Seite etwas löscht,
  bekommt es beim nächsten Lauf von der anderen Seite zurück.
- Kopiert wird mit `robocopy`, das in Windows schon eingebaut ist.
