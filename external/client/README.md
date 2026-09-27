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

Den gemeinsamen Protokollcode (`src/shared/MumbleProtocol.h`, `src/shared/VoiceProtocol.h`) nutzen `voice.dll` und das AzerothCore-Modul gemeinsam.

## Stand Phase 7

**Was funktioniert:**
- **Übertragung:** Mikrofon → Opus → Murmur → andere Clients → Lautsprecher.
- **Positional Voice:** Jede Sprachnachricht trägt die Position des Sprechers. Beim Hörer wird nach Entfernung (bis `MinDistance` voll, ab `MaxDistance` stumm) und Richtung gemischt, mit Stereo-Panning und dumpferem Klang von hinten.
- **Server-Anbindung (`[Server] Mode=auto`):**
  - Beim Betreten der Welt fragt `voice.dll` über die WoW-Verbindung beim Worldserver an.
  - Hat der Server `mod-voicechat`, kommen Murmur-Adresse, Name, Channel (Map/Instanz) und Rechte von dort.
  - Die Mumble-Session wird per Einmal-Nonce an den Charakter gebunden.
  - Ohne Antwort (Server ohne Modul) läuft alles eigenständig wie in Phase 4.
- **Gruppe/Raid:** Mitglieder sind immer voll hörbar, auch auf anderen Maps oder in Instanzen. Ihre Position bestimmt nur die Richtung: nie leiser, keine Dämpfung von hinten, keine Occlusion. Mitglieder auf einer anderen Map klingen mittig.
- **Fremde:** Sie sind nur hörbar, wenn der Server sie als „in Hörweite“ meldet, und werden mit der Entfernung leiser.
- **Occlusion:** Wände, Gebäude und Gelände zwischen dir und einem Fremden machen dessen Stimme leiser und dumpfer. Einstellbar in `voice.ini [Occlusion]` (Pegel und Tiefpass bei voller Verdeckung).
  - Pro Sprecher gehen 3 Sichtstrahlen (Mitte, links, rechts) über WoWs eigenes `TraceLine`. Der Anteil blockierter Strahlen ergibt einen weichen Übergang an Ecken.
  - Die Strahlen laufen im Hauptthread mit festem Budget pro Frame und werden etwa alle 100 ms erneuert. Übergänge sind geglättet (~120 ms).
  - Gruppenmitglieder sind nie betroffen.
- **Senden:** Gesendet wird nur an „nah + Gruppe“ (Mumble-Whisper). Das spart Bandbreite, und ein manipulierter Client kann niemandem Audio aufzwingen.
- **Blizzard-Voice-Oberfläche** (wenn der Server `Voice.BlizzardUi = 1` hat und `voice.ini [Ui] NativeUi=1`):
  - **Voice-Optionsmenü:** Es erscheint unter *Interface → Sound & Voice → Voice*. voice.dll gibt es frei wie der Server beim Login, still: kein Event, und WoWs alte Voice-Engine wird nicht gestartet.
  - **Einstellungen:** Sie kommen aus dem Menü. „Voice-Chat aktivieren“ ist der Hauptschalter, dazu Mikrofon an/aus, Push-to-Talk oder Sprachaktivierung (mit Empfindlichkeit), die Push-to-Talk-Taste (auch mit Modifikatoren oder Maustaste 3–5), die Lautstärken für Mikrofon und Sprache sowie **Ein- und Ausgabegerät**. Die Gerätelisten im Menü sind die Geräte, die voice.dll tatsächlich nutzt (WASAPI). „Standard“ bedeutet das Windows-Standardgerät oder `InputDevice`/`OutputDevice` aus `voice.ini`. Ein Wechsel greift sofort, ohne Neuverbindung.
  - **Spielgeräusche absenken:** Solange jemand hörbar spricht, werden Ton, Musik und Umgebung weich auf die Stärke der Regler im Voice-Menü abgesenkt (Faktor auf deine normale Lautstärke). Das ist absturzsicher: Die Originalwerte stehen vorher in `voice.duck` und werden beim nächsten Start zurückgeschrieben. Abschaltbar mit `voice.ini [Ui] DuckGameSound=0`.
  - **Mikrofontest:** Aufnahme- und Abspielknopf im Voice-Menü samt Pegelanzeige (5 s aufnehmen, dann anhören, mit Mikrofon- und Sprachlautstärke aus dem Menü). Er funktioniert auch, bevor Voice verbunden ist.
  - **Sprecherliste oben links:** Die originale Blizzard-Liste zeigt, wer gerade spricht: Gruppenmitglieder und hörbare Fremde, du selbst nicht. Die Namen blenden kurz nach dem Sprechen aus.
  - **Sprecher-Symbole:** Am Spielerrahmen und an den Gruppenrahmen blinkt das originale Lautsprecher-Symbol, wenn du oder ein Gruppenmitglied sprichst. Gruppenmitglieder mit Voice bekommen das Voice-Symbol. Technisch erweitert eine Lua-Bridge `UnitIsTalking`/`GetVoiceStatus` und liefert `VOICE_START`, `VOICE_STOP` und `VOICE_STATUS_UPDATE` an alle Frames, die sie registriert haben, also auch an Addons.
- **Kein Code-Patch:** Spiel-Daten und Pakete werden im WoW-Hauptthread verarbeitet (gesubclasstes WoW-Fenster). Für die Serverpakete wird nur ein Handler in WoWs Handler-Tabelle eingetragen, fremde Pakete gehen an den Original-Handler.

**Bewusst noch nicht enthalten:**
- **Adressen nur statisch geprüft:** Alle Client-Adressen stehen in `voice/WowApi.h`. Sie sind an der originalen 12340-Exe **statisch geprüft** (Disassembly: Handler-Tabelle `conn+0x53C`, Aufruf `cdecl(param, opcode, time, CDataStore*)`), aber noch **nicht zur Laufzeit**.

**Hier getestet:**
- **Übertragung:** voicecli → Murmur 1.5.517 → voicecli über UDP und über den TCP-Tunnel: 200/200 Pakete, 199 saubere 440-Hz-Frames.
- **Positionen über Murmur:** Ein Sprecher in 5 yd ist hörbar. In 100 yd ist er stumm, obwohl Pakete ankommen. Auf einer anderen Map ist er stumm, weil Murmur die Position entfernt.
- **Bot + Murmur** (`external/module-tests`):
  - Bindung per Nonce funktioniert.
  - Spieler landen im richtigen Channel.
  - Verschiedene Channels hören sich nicht.
  - Ungebundene Nutzer werden gekickt.
  - Flüstern über Channel-Grenzen (Gruppe) kommt an.
- **Lua-Bridge:** Mit Lua 5.1 wie in WoW (`tests/nativeui_test.sh`) werden Overrides, Event-Zustellung (auch nach einem fehlerhaften Handler), die Sprecherliste, die Geräteliste, „keine Events ohne Änderung“ und das einmalige Eintragen des Menüs geprüft.
- **Unit-Tests:** Entfernung, Panning, Drehung, hinten/vorne, Occlusion-Formel, Gruppen-Modus (nie leiser), Koordinaten und knackfreie Lautstärke-Rampen. Dazu der Mikrofontest (Aufnahme, Auto-Stopp, Wiedergabe, Pegel, Gain/Lautstärke), das Absenken der Spielgeräusche (Überblenden, Reglerbewegung, Ausloggen, Absturz) und der Occlusion-Tracker mit einer Test-Wand: voll, frei, teilweise (1/3), Strahlbudget, Glättung und Aufräumen.
- **Builds:** `voice.dll` wird fehlerfrei als 32-Bit-DLL gebaut, und das AzerothCore-Modul kompiliert ohne Warnungen.
- **In WoW selbst ist nichts getestet.**

## voice.dll bekommen

- **Zum Testen den MSVC-Build nehmen** (Actions-Artefakt oder Visual Studio): Nur er fängt Zugriffsverletzungen beim Zugriff auf WoW ab. Passt eine Adresse nicht, schreibt er eine Meldung in `voice.log` und schaltet die Spielanbindung ab, statt WoW abstürzen zu lassen. MinGW-Builds haben diesen Schutz nicht.
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
2. `voice.dll` neben `Wow.exe` legen. `voice.ini` (Kopie von `voice/voice.ini.example`) ist optional.
3. **Mit AzerothCore-Modul:** Es ist nichts weiter nötig, `Mode=auto` holt alles vom Server (Einrichtung siehe Haupt-README).
   **Ohne Modul:** In `voice.ini` bei `[Server]` `Mode=standalone` und den Murmur-Server eintragen.
4. Murmur braucht `opusthreshold=0`, damit Opus sofort aktiv ist. Das ist bei aktuellen Versionen Standard.
5. WoW starten, einloggen und die Push-to-Talk-Taste halten (Standard: CAPSLOCK).

## Test-Checkliste (bitte `voice.log` und das Worldserver-Log zurückmelden)

- [ ] Startet WoW normal, mit und ohne `voice.dll`?
- [ ] Steht `voice.dll started (phase 7)` in `voice.log`?
- [ ] Steht nach dem Betreten der Welt `ServerLink: SMSG handler installed` im Log?
- [ ] **Mit Modul:** Erscheinen im Log nacheinander:
  - [ ] `server config: <host>:<port> as '<Name>'`
  - [ ] `connected, session N`
  - [ ] `bound to the character`
  - [ ] `context wow335|<Realm>|<Map>|<Instanz>`

  Im Worldserver-Log sollte `<Name> bound to Mumble session N` stehen.
- [ ] **Ohne Modul** (`Mode=auto`): Kommt nach ca. 15 s `no answer from mod-voicechat ... -> standalone`, und verbindet der Client dann mit `voice.ini`?
- [ ] Steht `UDP active` im Log?
- [ ] **Fremde:** Hören sich zwei Clients (nicht in einer Gruppe) gegenseitig? Wird es leiser, wenn man weggeht, und ist es ab ca. 40 yd stumm? Kommt die Stimme von der richtigen Seite?
- [ ] **Gruppe:** Hört man ein Gruppenmitglied auch in 200 yd noch voll und aus der richtigen Richtung? Hört man es auch, wenn es in einer Instanz oder auf einem anderen Kontinent ist?
- [ ] **Blizzard-Oberfläche:**
  - [ ] Steht `NativeUi: Blizzard voice options unlocked` im Log, und gibt es unter *Interface → Sound & Voice* den Punkt *Voice*?
  - [ ] Ist Voice aus, bis „Voice-Chat aktivieren“ angehakt ist (Log: `voice chat is off in the WoW options`)?
  - [ ] Verbindet es nach dem Anhaken?
  - [ ] Funktionieren die Push-to-Talk-Taste aus dem Menü (Log: `push-to-talk: WoW binding '…'`), die Sprachaktivierung und die Lautstärkeregler?
  - [ ] Zeigen die Geräte-Menüs deine Mikrofone und Lautsprecher, und wechselt voice.dll bei Auswahl (Log: `audio devices changed in the voice menu`)?
  - [ ] Werden Ton, Musik und Umgebung leiser, solange jemand spricht (Regler „Ton/Musik/Umgebung“ im Voice-Menü)? Kommen sie danach wieder auf deine normalen Werte? Nach einem WoW-Absturz während jemand spricht: Stehen die Lautstärken beim nächsten Start wieder richtig (Log: `ducking: restored game volumes`)?
  - [ ] Nimmt der Mikrofontest im Voice-Menü auf, zeigt dabei den Pegel und spielt die Aufnahme ab?
  - [ ] Erscheinen Sprechende oben links in der Sprecherliste (auch Fremde in Hörweite) und verschwinden kurz danach wieder?
  - [ ] Blinkt das Lautsprecher-Symbol am eigenen Rahmen und am Gruppenrahmen beim Sprechen?
  - [ ] Funktioniert das auch nach `/reload`, ohne dass *Voice* doppelt im Menü steht?
  - [ ] Startet WoWs alte Voice-Engine trotzdem? Das sieht man z. B. an einer Meldung „Voice-Chat nicht verfügbar“ oder daran, dass das Mikrofon doppelt belegt ist.
- [ ] **Occlusion:** Wird ein Fremder hinter einer Hauswand oder einem Hügel leiser und dumpfer, und kommt der Klang beim Hervortreten weich zurück? Bleibt ein Gruppenmitglied hinter der Wand unverändert? Taucht `occlusion: access violation` im Log auf, passt die Adresse von `TraceLine` nicht, dann bitte melden.
- [ ] Wechselt beim Betreten einer Instanz der Kontext, und hören sich Spieler in verschiedenen Instanzen derselben Dungeon-ID nicht (außer in derselben Gruppe)?
- [ ] Funktionieren `.voice status`, `.voice mute <Name>`, `.voice unmute <Name>` und `.voice kick <Name>`?
- [ ] Wird beim Logout getrennt (`disconnect: left world`) und das Mikrofon freigegeben?
- [ ] Verbindet der Client neu, wenn Murmur oder der Worldserver neu startet?

Mit `voicecli.exe --host <server> --user Test --send-tone 440 --seconds 10` hört man im Spiel einen Testton, ohne ein zweites WoW zu brauchen. Das geht nur im Modus standalone, denn mit dem Modul spielt der Client nur Stimmen ab, die der Server meldet.
