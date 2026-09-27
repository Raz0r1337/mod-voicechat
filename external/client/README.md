# mod-voicechat – Client (voice.dll)

> ⚠️ **WARNUNG: Dieses Projekt befindet sich in einem sehr frühen Stadium. Alles ist noch ungetestet und ungeprüft. Es ist NICHT zum Spielen freigegeben!**

🇬🇧 [English version](README.en.md)

## Bestandteile

| Ordner | Inhalt |
|---|---|
| `voicecore/` | Mumble-1.5-Client: TLS (mbedTLS), OCB2-AES128 (kompatibel mit Murmur), UDP mit TCP-Tunnel-Fallback, Opus, Jitterbuffer und Mixer, PTT/VAD |
| `voice/` | `voice.dll` für Wow.exe 3.3.5a: Audio über WASAPI (miniaudio), Push-to-Talk, automatisches Verbinden beim Betreten der Welt |
| `tools/voicecli/` | Test-Client ohne Oberfläche (Ton senden und prüfen) |
| `tests/e2e.sh` | Ende-zu-Ende-Test gegen einen echten Murmur |
| `loader/` | Exe-Patch, damit Wow.exe `voice.dll` lädt (Dateigröße bleibt gleich) |

Den gemeinsamen Protokollcode (`src/shared/MumbleProtocol.h`) nutzt später auch das AzerothCore-Modul.

## Stand Phase 4

**Was funktioniert:**
- Mikrofon → Opus → Murmur → andere Clients → Lautsprecher
- **Positional Voice:** Jede Sprachnachricht trägt die Position des Sprechers. Beim Hörer wird nach Entfernung (bis `MinDistance` voll, ab `MaxDistance` stumm) und Richtung gemischt, mit Stereo-Panning und dumpferem Klang von hinten.
- **Trennung nach Map:** Der Mumble-Kontext ist `wow335|<Map-ID>`. Murmur gibt Positionen nur innerhalb derselben Map weiter, Sprecher ohne Position sind stumm.
- **Verbinden und Trennen:** Verbinden beim Betreten der Welt (Name = Charaktername), Trennen beim Logout, Reconnect mit Backoff.
- **Kein Code-Patch für den Hauptthread:** Spiel-Daten werden im WoW-Hauptthread gelesen. Dafür wird das WoW-Fenster gesubclassed; einen Code-Patch braucht das nicht.

**Bewusst noch nicht enthalten:**
- **Keine Instanz-Trennung und keine Rechte:** Das kommt mit der AzerothCore-Anbindung (Phase 5), ebenso das Beschränken auf Spieler in der Nähe (Bandbreite).
- **Keine Wände/Occlusion** (Phase 6).
- **Eigene PTT-Taste:** Die Taste steht in `voice.ini`, die WoW-Tastenbelegung wird noch nicht genutzt (Phase 7).
- **Adressen nur statisch geprüft:** Alle Client-Adressen stehen in `voice/WowApi.h`. Sie sind an der originalen 12340-Exe **statisch geprüft**, aber noch **nicht zur Laufzeit**.

**Hier getestet:**
- **Übertragung:** voicecli → Murmur 1.5.517 → voicecli über UDP und über den TCP-Tunnel: 200/200 Pakete, 199 saubere 440-Hz-Frames.
- **Positionen über Murmur:** Ein Sprecher in 5 yd ist hörbar. In 100 yd ist er stumm, obwohl Pakete ankommen. Auf einer anderen Map ist er stumm, weil Murmur die Position entfernt.
- **Unit-Tests:** Entfernung, Panning, Drehung, hinten/vorne, Occlusion-Formel, Koordinaten und knackfreie Lautstärke-Rampen.
- `voice.dll` wird fehlerfrei als 32-Bit-DLL gebaut und importiert nur System-DLLs.
- **In WoW selbst ist nichts getestet.**

## voice.dll bekommen

- **Ohne Bauen:** Im Repo unter *Actions → client → letzter Lauf → Artifacts → `voice-win32`* liegen `voice.dll`, `voicecli.exe`, die Beispiel-`voice.ini` und der Loader.
- **Selbst bauen (Windows, Visual Studio 2022):**
  ```bat
  cmake -S external/client -B build -A Win32
  cmake --build build --config Release
  ```
- **Selbst bauen (Linux, MinGW-Cross-Build):**
  ```sh
  cmake -S external/client -B build-win32 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw32.cmake -DCMAKE_BUILD_TYPE=Release
  cmake --build build-win32
  ```

## In einen Testclient einbauen

1. Wow.exe mit dem Loader patchen, siehe [`loader/README.md`](loader/README.md).
2. `voice.dll` und `voice.ini` (Kopie von `voice/voice.ini.example`) neben `Wow.exe` legen.
3. In `voice.ini` den Murmur-Server eintragen.
4. Murmur braucht `opusthreshold=0`, damit Opus sofort aktiv ist. Das ist bei aktuellen Versionen Standard.
5. WoW starten, einloggen und die Push-to-Talk-Taste halten (Standard: CAPSLOCK).

## Test-Checkliste (bitte `voice.log` zurückmelden)

- [ ] Startet WoW normal, mit und ohne `voice.dll`?
- [ ] Steht `voice.dll started` in `voice.log`?
- [ ] Erscheint beim Betreten der Welt `connecting as '<Charaktername>'` und danach `connected, session N`? Falls nicht, zum Testen `AutoConnectInWorld=0` und `Username=Test` setzen und `voice.log` schicken (dann passt eine Adresse in `WowApi.h` nicht).
- [ ] Steht `UDP active` im Log?
- [ ] Hören sich zwei Clients gegenseitig? Wird es leiser, wenn man weggeht, ab ca. 40 yd stumm? Kommt die Stimme von der richtigen Seite?
- [ ] Steht beim Betreten einer anderen Map `context wow335|<Map>` im Log?
- [ ] Wird beim Logout getrennt (`disconnect: left world`) und das Mikrofon freigegeben?
- [ ] Verbindet der Client neu, wenn Murmur neu startet?

Mit `voicecli.exe --host <server> --user Test --send-tone 440 --seconds 10` hört man im Spiel einen Testton, ohne ein zweites WoW zu brauchen.
