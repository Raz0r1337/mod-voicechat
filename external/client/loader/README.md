# voice.dll-Loader für Wow.exe 3.3.5a

🇬🇧 [English version](README.en.md)

Kleiner Exe-Patch, damit `Wow.exe` beim Start `voice.dll` lädt. Kein Injector und kein Launcher nötig.

## So funktioniert es

- Der Patch hängt eine eigene PE-Sektion **`.vcl`** an (512 Byte) und biegt **nur den Einstiegspunkt** im PE-Header darauf um.
- Der Stub ruft `LoadLibraryA("voice.dll")` über die vorhandene Importtabelle auf und springt danach zum bisherigen Einstiegspunkt.
- **Fehlt `voice.dll`, startet WoW ganz normal.**
- In `.text`, `.rdata` und `.data` wird **kein Byte** verändert. Das kollidiert also nicht mit:
  - Code-Caves im Padding
  - Detours
  - dem AwesomeWotlk-Loader (`0x40B7D0` / `0x4E5CB0`)
  - anderen angehängten Sektionen (`.hdp`, `.camr`)
- Die Reihenfolge ist egal (vor oder nach St0ny's Patcher). Mehrfach ausführen ist harmlos, weil der Patch `.vcl` wiedererkennt.

Geprüft an der originalen 12340-Exe (SHA256 `AA63A575…8CB8`), einmal pur und einmal nach allen 34 aktiven Patches aus St0ny's Patcher. Geändert werden nur NumberOfSections, AddressOfEntryPoint, SizeOfImage, ein neuer Sektionseintrag und die angehängten Bytes.

## Benutzung

**Eigenständig:** `Add-VoiceLoader.ps1`, `voice_patcher.ps1` und `voice_patcher.bat` in den WoW-Ordner kopieren und `voice_patcher.bat` starten. Vorher wird `Wow.exe.voice.BAK` angelegt.

```powershell
powershell -ExecutionPolicy Bypass -File voice_patcher.ps1 [-Path Wow.exe] [-DllName voice.dll]
```

**Eingebaut in St0ny's `apply_patches.ps1`:** `Add-VoiceLoader.ps1` daneben legen und Folgendes ergänzen:

```powershell
# oben, nach den Helfer-Funktionen:
. "$PSScriptRoot\Add-VoiceLoader.ps1"

# in der $patches-Liste:
,@('voice.dll laden (mod-voicechat)', {
    $script:f = Add-VoiceLoader -Image $script:f
})
```

## Hinweis

Die Wow.exe ist Eigentum von Blizzard und wird **nicht** mit diesem Projekt verteilt. Der Patcher verändert nur deine lokale Kopie.
