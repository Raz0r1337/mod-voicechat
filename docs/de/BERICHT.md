# mod-voicechat – Recherchebericht, Architektur und Implementierungsplan

🇬🇧 [English version](../en/REPORT.md)

Stand: 2026-09-27 · Ziel: WoW 3.3.5a (Build 12340) + AzerothCore + Murmur, natives 3D-Proximity-Voice ohne externe Voice-Software.

**Legende:** ✅ belegt (Quelle geprüft) · 🟡 starke Indizien · ❓ Hypothese, muss am Client verifiziert werden

---

## 1. Zusammenfassung

- Der 3.3.5a-Client enthält das komplette alte Blizzard-Voice-System, intern **„Comsat“**: UI, Lua-API, CVars, Opcodes und eine funktionsfähige Engine (Aufnahme, Codec, UDP-Transport). Andere Emulatoren haben es 2007 (Ascent) und 2024 (CMaNGOS-PR, Reviewer-Test mit 3.3.5) wieder zum Laufen gebracht.
- **Für unser Ziel ist die alte Engine trotzdem nicht brauchbar.** Sie kann nur Gruppen-, Raid-, BG- und Custom-Channel-Voice ohne Positionen, nutzt einen unbekannten proprietären Codec und ein eigenes UDP-Relay-Protokoll. Murmur akzeptiert dagegen nur Opus.
- **Wiederverwendbar ist die Oberfläche:** Blizzard-Voice-UI, Lua-API, CVars und Keybinding (Push-to-Talk). Wir ersetzen die Engine dahinter durch eine eigene in `voice.dll`.
- **Murmur macht kein 3D.** Es leitet Positionsdaten nur weiter, und zwar nur zwischen Nutzern mit identischem Kontext. Distanz, Richtung, Occlusion und Filter berechnet ohnehin der empfangende Client. Die sinnvolle Aufteilung ist also ein Hybrid: **Murmur für Transport, Routing und Isolation, der WoW-Client für die gesamte Audiowiedergabe im Raum**.
- **Isolation nach Map und Instanz muss serverseitig passieren.** Der Client kennt seine Instanz-ID nicht, nur AzerothCore kennt sie. AzerothCore verbindet sich als privilegierter Mumble-Bot und verschiebt Nutzer in Channels nach dem Schema `Map/Instanz`.
- Die Auth kommt ohne Ice-Authenticator aus. Die Bindung zwischen Mumble-Session und WoW-Charakter läuft über eine einmalige Nonce: WoW-Verbindung → AC, Mumble-TLS → Bot.
- Das AzerothCore-Modul braucht **keine Core-Änderungen**, alles läuft im Modul. Die Voice-Stubs im Core nutzen wir nicht, das Modul fängt die Pakete vorher per Hook ab. Moderation (Mute, Silence, Kick) läuft über MVCP und GM-Befehle statt über die gesperrten `STATUS_NEVER`-Opcodes.

---

## 2. Bestandsaufnahme (Phase 1)

### 2.1 WoW-3.3.5a-Client

| Befund | Status | Quelle |
|---|---|---|
| Voice-UI komplett (`VoiceChat.lua/xml`, Options-Panel „Voice“, Sprecherliste `VoiceChatTalkers`, Minimap-Voice-Button, Channel-Pullout, Mute-Liste, Sprechersymbole bei Party/Raid) | ✅ | 3.3.5-FrameXML (wowgaming/3.3.5-interface-files) |
| Lua-API, u. a. `IsVoiceChatEnabled`, `IsVoiceChatAllowedByServer`, `VoiceIsDisabledByClient`, `GetNumVoiceSessions`, `GetVoiceSessionInfo`, `GetVoiceSessionMemberInfoBySessionID`, `GetVoiceStatus`, `UnitIsTalking`, `SetActiveVoiceChannelBySessionID`, `VoiceEnumerateCaptureDevices/OutputDevices`, `VoiceSelectCaptureDevice/OutputDevice`, `VoiceChat_StartCapture`/Loopback-Test, `VoiceChat_GetCurrentMicrophoneSignalLevel`, `AddMute/DelMute/GetMuteStatus`, `SetSelfMuteState`, `ChannelSilenceVoice` | ✅ | FrameXML |
| Events: `VOICE_START/STOP`, `VOICE_STATUS_UPDATE`, `VOICE_CHAT_ENABLED_UPDATE`, `VOICE_PUSH_TO_TALK_START/STOP`, `VOICE_SELF_MUTE`, `VOICE_SESSIONS_UPDATE`, `VOICE_CHANNEL_STATUS_UPDATE`, `VOICE_LEFT_SESSION`, `VOICE_PLATE_START/STOP`, `MUTELIST_UPDATE`, `CHANNEL_VOICE_UPDATE` | ✅ | FrameXML |
| CVars: `EnableVoiceChat`, `EnableMicrophone`, `VoiceChatMode` (PTT/Sprachaktivierung), `PushToTalkButton`, `PushToTalkSound`, `VoiceActivationSensitivity`, `OutboundChatVolume`, `InboundChatVolume`, `ChatSoundVolume/ChatMusicVolume/ChatAmbienceVolume` (Game-Audio-Absenkung), `VoiceChatSelfMute`, `Sound_VoiceChatInputDriverIndex`, `Sound_VoiceChatOutputDriverIndex` | ✅ | FrameXML |
| Engine-Name „Comsat“; deaktiviert ohne SSE oder bei zweiter laufender WoW-Instanz | ✅ | Kommentar in `AudioOptionsPanels.lua` |
| Voice-Optionen erscheinen nur, wenn der Server es erlaubt (`IsVoiceChatAllowedByServer`, gesetzt über `SMSG_FEATURE_SYSTEM_STATUS`) | ✅ | FrameXML + AC `CharacterHandler.cpp` |
| Die Engine funktioniert in 3.3.5a: Aufnahme → Codec → Verschlüsselung (16-Byte-Key, Nullkey wird akzeptiert) → UDP zum Server aus dem Roster-Paket | 🟡 | Ascent 2007; CMaNGOS-PR #668 (2024), Reviewer „auf WotLK 3.3.5 getestet“ |
| Codec der Comsat-Engine | ❓ unbekannt | Öffentlich nicht dokumentiert. Für uns irrelevant (siehe §5) |
| Max. 5 gleichzeitige Sprecher, 1 aktiver Voice-Channel | ✅ | Blizzard-Doku 2008 (Web-Archiv, zitiert im voicechat-server-Wiki) |

**UDP-Format der alten Engine** (aus Ascents Relay): Byte 4 = User-Slot, Bytes 5–6 = Channel-ID, 7-Byte-Paket = Registrierung, Rest = verschlüsselte Nutzlast. Das Relay dekodiert nichts, es verteilt nur an die anderen Slots.

### 2.2 AzerothCore (Stand `3d28fa7`, 2026-09-27)

| Befund | Status |
|---|---|
| Alle Voice-Opcodes definiert (`Opcodes.h` 0x39E–0x3FC) | ✅ |
| Nur 3 Handler, alles Stubs, die Bytes überspringen: `CMSG_VOICE_SESSION_ENABLE` (AUTHED), `CMSG_SET_ACTIVE_VOICE_CHANNEL` (AUTHED), `CMSG_CHANNEL_VOICE_ON` (LOGGEDIN) | ✅ |
| Übrige Voice-CMSGs (`CMSG_CHANNEL_SILENCE_VOICE`, `CMSG_ADD/DEL_VOICE_IGNORE`, `CMSG_CHANNEL_VOICE_OFF`, `CMSG_VOICE_SET_TALKER_MUTED_REQUEST`) sind `STATUS_NEVER` und werden **vor** dem Modul-Hook verworfen | ✅ `WorldSession.cpp` |
| `SMSG_FEATURE_SYSTEM_STATUS` schickt fest „Voice = 0“ (zwei Stellen in `CharacterHandler.cpp`) | ✅ |
| `ACCOUNT_FLAG_DISABLE_VOICE` / `_DISABLE_VOICE_SPEAK` existieren, sind aber nicht implementiert | ✅ |
| Modul-Hooks: `ServerScript::CanPacketReceive/CanPacketSend` (Pakete lesen und unterdrücken), `OnPlayerLogin/Logout`, `AllMapScript::OnPlayerEnterAll/LeaveAll` (auch bei Instanzwechsel), `GroupScript`, `OnPlayerJoinBG`, `WorldScript::OnUpdate/OnStartup` | ✅ |
| AC kompiliert nur `modules/<modul>/src/**`, übernimmt `conf/*.conf.dist` und spielt `data/sql/db-*` automatisch ein | ✅ `ConfigureModules.cmake`, `UpdateFetcher.cpp` |
| Eigene Opcodes ≥ 0x521 lehnt der Core ab (`IsValidOpcode`) | ✅ |

**Issue #5063** (ReynoldsCahoon, 2021) ist ein reiner Feature-Wunsch ohne Technik. Sie verlinkt TrinityCore #15057 (ebenfalls ohne Technik) und **Ascent-Classic `ascent-voicechat`**, die einzige echte historische Implementierung.

### 2.3 Historische Implementierungen

- **Ascent (2007, Burlex):** Der Worldserver verbindet sich per TCP mit einem separaten Voice-Relay (eigenes Mini-Protokoll: Channel anlegen/löschen, Slot aktivieren/deaktivieren, Ping). Der Worldserver schickt dem Client `SMSG_VOICE_SESSION_ROSTER_UPDATE` mit Session-ID, Channel-ID, Typ, Name, 16-Byte-Key, IPv4 und Port des Relays sowie der Mitgliederliste (GUID, Slot, Flags).
- **celguar/voicechat-server + CMaNGOS mangos-tbc PR #668 (2024, offen):** Aufbauend auf Ascent funktionieren Party, Raid, BG, Custom Channels, Mute und Silence. Das vollständige Format von Roster, `SMSG_AVAILABLE_VOICE_CHANNEL`, `SMSG_VOICE_SESSION_LEAVE`, `SMSG_VOICE_CHAT_STATUS` und `SMSG_VOICESESSION_FULL` ist dort im Code dokumentiert. Proximity ist nicht implementiert.
- TrinityCore und MaNGOS-Mainline haben keine Voice-Implementierung.

### 2.4 ReynoldsCahoon/WotLK-Mumble-Positional-Voice

**`wow3.dll`** (`plugin/wow3.cpp`) ist ein klassisches Mumble-Positional-Plugin. Es liest per `peekProc` 9 feste Adressen aus einem **fremden** Prozess:

| Wert | Adresse | Anmerkung |
|---|---|---|
| In-Game-Status | `0xBD0792` | 1 = im Spiel |
| Position | `0xADF4E4` | wird auch als Kameraposition verwendet |
| Blickrichtung (Heading) | `0xBEBA70` | auch als Kamerarichtung verwendet |
| Kamera-Front/-Top | `0xADF5F0` / `0xADF554` | |
| Charaktername | `0xC79D18` | |
| Map-ID | `0xAB63BC` | WotLK-Extensions nennt `0xBD088C` ❓ klären |
| Leader-GUID | `0xBD1968` | nur als `int` gelesen, also auf 32 Bit abgeschnitten |

- **Koordinaten:** Mumble X = −WoW Y, Mumble Y = WoW Z, Mumble Z = WoW X.
- **Kontext und Identität:** Kontext ist `{"map": id}`, Identität ist `{"char": name, "leaderguid": n}`.

**`mumo-module/wowrp.py`** legt Channels an (Proximity Groups → Waiting Room, Overworld/Kontinente, Group Channels) und verschiebt Nutzer nach Map-ID. Wer in einem Dungeon ist, landet im Channel der Leader-GUID. Schwachstellen:
- Keine Authentifizierung; Kontext und Identität kann der Client beliebig fälschen.
- Keine echte Instanztrennung, getrennt wird nur über die Leader-GUID.
- `eval()` auf Konfigwerte.
- Cleanup-Bug: der Channel wird gelöscht, bevor sein Eintrag bereinigt ist.
- Kein Schutz gegen *Listen*-Berechtigungen.

**Übernehmen** lassen sich die Koordinaten-Konvention, die Idee „Kontext = Welt“ und die Channel-Hierarchie. **Nicht übernommen** werden das Auslesen fremder Prozesse, das Vertrauen in Client-Daten und Python/Ice.

### 2.5 Murmur (Mumble-Server, Quellcode `7bbd2c1`)

| Befund | Status |
|---|---|
| Transport: TLS-TCP (Protobuf `Mumble.proto`) + UDP-Voice (ab 1.5 Protobuf `MumbleUDP.proto`, vorher Legacy-Format), verschlüsselt mit **OCB2-AES128**, Fallback UDP über TCP (`UDPTunnel`) | ✅ |
| Opus-Pflicht: im Opus-Modus verwirft der Server jedes Nicht-Opus-Paket | ✅ `Server.cpp` |
| **Server rendert kein 3D**: Positionsdaten werden nur weitergereicht, wenn `sender.ssContext == receiver.ssContext`. Keine Distanzfilterung | ✅ `AudioReceiverBuffer.cpp` |
| Routing: gleicher Channel + verlinkte Channels (transitiv!) + Channel-Listener + VoiceTargets (Whisper/Shout an Sessions/Channels/Gruppen) | ✅ |
| Whisper an Sessions erfordert `Whisper`-Recht **im Channel des Empfängers** | ✅ |
| `Move`-Recht des Verschiebenden hebelt fehlendes `Enter` des Ziels aus | ✅ `Messages.cpp` |
| **`Listen`-Recht** erlaubt, fremde Channels mitzuhören, ohne sie zu betreten, und muss deshalb gesperrt werden | ✅ |
| Remote-Steuerung nur über ZeroC Ice (kein gRPC mehr im Code) | ✅ |
| Schutz: Bandbreitenlimit pro Nutzer, `messagelimit/burst`, Autoban, `users`, `channelcountlimit` (Default 1000) | ✅ |

---

## 3. Architektur (Phase 2)

### 3.1 Gesamtbild

```text
┌──────────────────────── WoW.exe (3.3.5a, gepatcht: lädt voice.dll) ───────────────────────┐
│ Blizzard-Voice-UI (FrameXML) ⇄ Lua-API-Overrides ⇄ voice.dll                             │
│   voice.dll:                                                                             │
│   ├─ WoW-Bridge (Hauptthread): Position/Orientierung, Map, Objektmanager (GUID→Position), │
│   │   Raycasts (Occlusion), CVars, PTT, eigenes Steuerprotokoll MVCP über den WoW-Socket  │
│   ├─ Audio (eigener Thread): miniaudio (WASAPI) → speexdsp (AGC/Denoise/VAD) → Opus      │
│   ├─ Mumble-Client: TLS (Control) + UDP/OCB2 (Voice), VoiceTargets, Kontext               │
│   └─ Renderer: pro Sprecher Jitterbuffer → Opus-Dekoder → Distanz · Richtung · Occlusion  │
│      (Tiefpass) → Stereo/HRTF-Mix → Ausgabe                                               │
└──────────────┬───────────────────────────────────────────────────────┬───────────────────┘
               │ WoW-Protokoll (MVCP in Voice-Opcodes)                  │ Mumble (TLS 64738 + UDP)
               ▼                                                        ▼
┌──── AzerothCore worldserver + mod-voicechat ────┐         ┌──────── Murmur ─────────┐
│ VoiceManager: Rechte, Session-Binding, Kontext  │ Mumble  │ Channels = Map/Instanz  │
│ MumbleBot (Asio-Thread): Channels anlegen,      │◄──TLS──►│ ACL: kein Enter/Listen, │
│ Nutzer verschieben/muten/kicken                 │         │ Whisper nur „in“        │
└─────────────────────────────────────────────────┘         └─────────────────────────┘
```

### 3.2 Variante A vs. B: Ergebnis

| | A: „Mumble macht 3D“ | B: „Client macht alles“ | **Gewählt: Hybrid** |
|---|---|---|---|
| Wer rendert Distanz und Richtung? | In Mumble der **Client** (Murmur nicht!), also ohne Mumble-Client niemand | voice.dll | voice.dll |
| Isolation (Map/Instanz) | Kontext ist client-gesetzt und damit fälschbar | müsste der Client selbst erzwingen | **Murmur-Channels, serverseitig durch AC gesetzt** |
| Bandbreite | Alle im Channel hören alle | Client entscheidet selbst | **VoiceTarget = Liste naher Sessions** (Client), Whisper-ACL begrenzt auf den eigenen Channel |
| Occlusion | nicht möglich | Raycast im Client | Raycast im Client |

Variante A setzt einen Mumble-Client voraus, der das Rendering übernimmt. Da es keinen gibt, fällt diese Aufgabe zwangsläufig voice.dll zu. Murmur liefert dafür sicheres Routing, Channels, ACL und die Positionsdaten im Paket.

### 3.3 Positionen

- **Primärquelle ist der Objektmanager des Clients** (Mumble-Session → Charakter-GUID → Einheitenposition). Das ist fälschungssicher (Server-Positionen), flüssig (dieselbe Interpolation wie das Modell) und kostet kein Protokoll.
- **Fallback** ist `positional_data` im Opus-Paket (Mumble-Konvention: Meter, Y oben). Das greift für Spieler außerhalb der Sichtweite, die im Channel aber noch per VoiceTarget erreichbar sind, und für Debugging mit dem Desktop-Mumble.
- **Hörer:** Position = Charakter, Orientierung = Kamera-Yaw. Umschaltbar auf die Blickrichtung des Charakters. ❓ Welche sich natürlicher anfühlt, entscheidet ein Test.
- **Kurve:** `gain = clamp(1 − (d − d_min)/(d_max − d_min))^k`. Defaults: `d_min` = 3 yd, `d_max` = 40 yd (Sagen-Reichweite in WoW: 25 yd, Schreien: 100 yd), konfigurierbar pro Server und über MVCP an die Clients verteilt.
- **Richtung:** Equal-Power-Panning im ersten Schritt, HRTF optional später.

### 3.4 Map-/Instanz-Isolation

- **Channel-Baum in Murmur** (vom Bot verwaltet):
  - `WoW/<Realm>/Map-<id>` für Kontinente, dauerhaft
  - `WoW/<Realm>/Map-<id>/Inst-<instanceId>` für Instanzen, BGs und Arenen, als **temporärer Channel** (Murmur löscht leere Channels automatisch)
  - optional mit Suffix `-A`/`-H`, wenn `Voice.CrossFaction = 0`
- **Kontext-String** (für Murmurs Positionsfilter): `wow|<realm>|<map>|<instance>`. Gesetzt wird er vom Client, aber nur mit dem Wert, den der Server per MVCP vorgibt. Die eigentliche Sicherheit liefert trotzdem der Channel.
- **ACL-Vorlage:**
  - Root verweigert `@all` die Rechte Enter, Listen, MakeChannel, MakeTempChannel, LinkChannel, Whisper, TextMessage und Register.
  - Der Bot bekommt Move, MuteDeafen, Kick, MakeChannel und Write.
  - Jeder Instanz-Channel erlaubt `@in` die Rechte Speak und Whisper.
  - Der Lobby-Channel erlaubt TextMessage nur an den Bot.
- **Phasing** (`PhaseMask`) ist optional als weitere Kontextstufe möglich. Standard ist aus, weil Phasen oft wechseln.

### 3.5 Authentifizierung und Lifecycle

```text
Login  : AC prüft Recht (Konfig, Account-Flag DISABLE_VOICE, GM-Level)
         Client → AC   MVCP HELLO(version)                 [nur mit voice.dll]
         AC → Client   MVCP CONFIG(host, port, srvpw?, nonce N (128 Bit, 60 s, einmalig), Kontext, Reichweite)
         Client → Murmur  TLS + Authenticate(name = Charaktername, eigenes Client-Zertifikat)
         Client → Bot     TextMessage "BIND N"   (Bot vergleicht in konstanter Zeit → bindet Session↔GUID)
         Bot → Murmur     UserState(move → Map/Instanz-Channel), bei DISABLE_VOICE_SPEAK: mute
Map-/Instanzwechsel : OnPlayerEnterAll → Bot verschiebt; AC → Client MVCP CONTEXT(seq, Kontext)
Logout / Disconnect : Bot kickt Session; Client trennt sich auch selbst
Ungebundene Session : Kick nach 15 s
Bot-Reconnect       : Backoff; danach neue Nonce für alle Online-Spieler (Rebind)
Murmur weg          : Client-Backoff-Reconnect; UI zeigt Status (VOICE_STATUS_UPDATE)
```

- Das Zertifikat erzeugt voice.dll selbst beim ersten Start (self-signed).
- Die Nonce geht nie über Lua, also sehen Addons sie nicht. Der Server schickt MVCP nur nach einem HELLO, damit Clients ohne voice.dll keine unbekannten Nutzlasten bekommen.

### 3.6 MVCP: Steuerkanal ohne Core-Änderung

- **Client → Server:** `CMSG_VOICE_SESSION_ENABLE` (0x3AF). Die ersten 2 Bytes haben die originale Bedeutung, danach folgt optional `'MVC1'` + TLV-Nachricht.
  - Das Modul liest das Paket in `CanPacketReceive` und unterdrückt den Stub-Handler.
  - Der originale Client sendet dieses Opcode selbst (2 Bytes) bei Voice-Einstellungen. Das Modul wertet das als EnableVoice/EnableMic.
- **Server → Client:** ein Voice-SMSG, dessen Handler voice.dll ersetzt (`CNetClient::SetMessageHandler`). Kandidat ist `SMSG_VOICE_SESSION_ADJUST_PRIORITY` (0x3A0) ❓. Sein Originalhandler läuft danach nie mehr.
- **Rate-Limit** pro Session im Modul.
- **Alternative für Debug/Fallback:** Addon-Messages (Whisper an sich selbst, Präfix `MVC`). Das braucht keinerlei Client-Hooks, ist aber für Lua und damit für Addons sichtbar, also nicht für Nonces verwenden.

### 3.7 Occlusion (Phase 6)

- **Wo:** im Client, weil die WoW-Geometrie (Terrain, WMO, M2) dort geladen ist. Raycast über die Client-Funktion `TraceLine` / `CWorld::Intersect` (Adresse laut Community `0x7A3B70` ❓, Flags für Terrain/WMO/M2 ❓).
- **Strahlen:** pro Sprecher 1 direkter Strahl Kopf→Kopf plus 2–4 versetzte Strahlen. Das ergibt einen Anteil freier Strahlen, also teilweise Occlusion.
- **Budget:** N Strahlen pro Frame reihum, Ergebnisse geglättet (~150 ms Attack/Release). Es läuft nur im Hauptthread, weil die Engine nicht thread-safe ist.
- **Formel:** `gain = distanz(d) · occlusion(o)`. Die Mapping-Tabelle (1.0 / 0.6 / 0.25 / 0.05) ist konfigurierbar, dazu **Tiefpass** mit `cutoff = lerp(20 kHz, 800 Hz, o)`.
- **Materialien:** Die Kollisionsflags unterscheiden Terrain, WMO und M2. Echte Materialien gibt es nicht, WMO-Gruppen- oder Doodad-IDs wären ein späterer Ansatz ❓.
- **Serverseitig** (AC-VMAP `isInLineOfSight`) ginge es auch, aber mit Latenz und Serverlast. Das kommt nur als Anti-Cheat-Option in Frage.

### 3.8 Threads und Robustheit

- **Client:**
  - Der Hauptthread (Hook auf eine Per-Frame-Funktion oder ein Lua-OnUpdate-Frame) schreibt einen Snapshot (Hörer, Sprecherpositionen, Occlusion) in einen Doppelpuffer.
  - Audio- und Netzwerkthreads rufen **nie** WoW-Funktionen auf.
  - Alle Hooks laufen in SEH/try; bei einem Fehler deaktiviert sich voice.dll und das Spiel läuft weiter.
- **AC:**
  - Der Bot läuft im eigenen `boost::asio`-Thread (Asio und OpenSSL sind in AC vorhanden).
  - Spielereignisse gehen über eine Thread-sichere Queue an den Bot (Map-Updates laufen in AC parallel!). Rückmeldungen verarbeitet `WorldScript::OnUpdate`.
  - Spieler werden nur per GUID nachgeschlagen (`ObjectAccessor`), nie als roher Zeiger über Threads hinweg gehalten.
  - Race-Schutz: Sequenznummern pro Kontextwechsel; der Bot verarbeitet nur den jeweils neuesten Stand pro GUID.

### 3.9 Sicherheit

| Risiko | Maßnahme |
|---|---|
| Fremde Instanzen mithören | Channel pro Instanz, kein Enter/Listen für Nutzer, Whisper nur `@in`, Kontext vom Server vorgegeben |
| Identität kapern | Nonce (128 Bit, 60 s, einmalig, konstante Vergleichszeit) über den WoW-Kanal, der bereits authentifiziert ist; Mumble-Name = Charaktername; Duplikate werden abgelehnt |
| Voice-Spam | Murmur-Bandbreitenlimit, Opus-Bitrate begrenzt, GM-Mute → Murmur-`mute`, `ACCOUNT_FLAG_DISABLE_VOICE(_SPEAK)` wird umgesetzt |
| DDoS | Murmur-Autoban, `messagelimit`, `users`; UDP-Port per Firewall/Rate-Limit schützen; ungebundene Sessions werden gekickt |
| Verschlüsselung | TLS 1.2+ für die Steuerung; OCB2-AES128 für UDP (OCB2 hat veröffentlichte Schwächen, Mumble enthält Gegenmaßnahmen, Stand der Technik bei Mumble) |
| Bot-Zugang | eigener registrierter Bot-Nutzer mit Minimalrechten statt SuperUser; Zugangsdaten nur in `mod_voicechat.conf` |
| Warden | Die eigenen Patches/Hooks können AC-Warden-Checks auslösen. Warden-Checks auf dem eigenen Server entsprechend konfigurieren |

---

### 3.10 Umsetzung Phase 5: Abweichungen vom Plan

| Plan | Umgesetzt | Grund |
|---|---|---|
| Instanz-Channels als temporäre Channels | **permanente** Channels, die der Bot aufräumt (`Voice.Bot.EmptyChannelTimeout`) | Murmur zieht den Ersteller eines temporären Channels hinein, also würde der Bot selbst verschoben |
| Root verweigert Whisper | **Whisper erlaubt** im Baum `WoW/`, Sprechen nur `@in`. Der Client spielt nur Stimmen ab, die der Server in NEARBY meldet | **Gruppe/Raid ist über Maps hinweg hörbar** (siehe unten). Ein manipulierter Client kann trotzdem niemandem Audio aufzwingen |
| Kick ungebundener Sessions nach 15 s | `Voice.BindTimeoutSeconds` (Standard 30 s), Ausnahmen per `Voice.AllowedExternalUsers` | Zeit für Verbindungsaufbau über UDP und TLS |
| Client-Zertifikat | noch keins; die Bindung läuft nur über die Einmal-Nonce (128 Bit, 60 s) | reicht für die Zuordnung, ein Zertifikat kann später dazukommen |
| Original-Handler von 0x3A0 läuft nie mehr | Pakete ohne `MVC1` gehen an den Original-Handler | weniger Eingriff |
| Kontext `wow\|realm\|map\|instance` | `wow335\|<Realm>\|<Map>\|<Instanz>[\|A/H]` | Build im Kontext |

**Gruppenregel** (Wunsch aus dem Projekt):
- **Gruppen- und Raidmitglieder** hört man immer in voller Lautstärke, auch auf anderen Maps oder in Instanzen. Die Position bestimmt nur die Richtung (Panning): keine Entfernungsdämpfung, keine Dämpfung von hinten, keine Occlusion. Ohne Position (andere Map) klingt die Stimme mittig.
- **Fremde** hört man nur, wenn der Server sie als „in Hörweite“ meldet, und mit zunehmender Entfernung leiser. In Instanzen ist man in der Regel nur mit der eigenen Gruppe, deshalb wirkt die Distanzregel praktisch nur in der offenen Welt.
- **Senden:** Der Client flüstert an „nah ∪ Gruppe“ (VoiceTarget 1). Der Server schickt beide Listen jede Sekunde (`NEARBY`: Feld 1 = nah, Feld 2 = Gruppe), und nur, wenn sie sich geändert haben.

**H3 (Handler-Übernahme):** Die Disassembly bestätigt:
- Die Tabelle liegt bei `NetClient+0x53C`, die Parameter bei `+0x19B8`.
- Der Dispatcher 0x631FE0 ruft `cdecl(param, opcode, time, CDataStore*)` auf.
- `SetMessageHandler` und `SendPacket` brechen ohne Verbindung mit Fatal Error ab, deshalb prüft voice.dll vorher `[0xC79CF4]`.

Der Laufzeittest steht noch aus.

## 4. Codec

1. **Welcher Codec in Comsat steckt**, ist ❓ unbekannt. Er ist in keiner erreichbaren Quelle dokumentiert und ließe sich durch statische Analyse (Strings/Signaturen in Wow.exe) bestimmen.
2. **Vorhanden ist er** 🟡, denn die Engine funktioniert in 3.3.5a.
3. **Brauchbar für Mumble ist er nicht:** Murmur akzeptiert nur Opus, und die Nutzlast ist proprietär. Umkodieren im Server wäre Unsinn.
4. **Pipeline** (aus FrameXML): Gerätewahl → Mikrofonpegel/Loopback-Test → PTT oder Sprachaktivierung (Schwelle) → Codec → UDP-Relay → Wiedergabe mit Absenkung der Spielgeräusche.
5. **Aufnahme und Wiedergabe von Comsat wiederverwenden?** Nicht sinnvoll: Das erfordert tiefes Reverse Engineering der Engine-Interna, und die Gewinne sind gering, weil miniaudio das in wenigen hundert Zeilen erledigt.
6. **Nur Codec oder Transport ersetzen?** Theoretisch per Hook denkbar, aber mit hohem RE-Risiko und ohne Positionsdaten. Wir ersetzen die **ganze Engine** und behalten **UI, API und CVars**.

**Opus-Integration:**
- **libopus** (BSD) statisch für Win32 (MSVC/MinGW), 48 kHz mono, Anwendungsmodus VOIP, 20-ms-Frames, 24–40 kbit/s, In-Band-FEC und DTX an, Paketverlust-Kaschierung (PLC) beim Dekodieren.
- **speexdsp** (BSD) für Rauschunterdrückung, AGC, VAD, optional Echo-Cancel, Jitterbuffer und Resampler.
- **miniaudio** (MIT-0) für WASAPI-Aufnahme und -Wiedergabe, Geräteauflistung und Tiefpass.

Alle Lizenzen sind GPL-2.0-kompatibel.

---

## 5. Antworten auf die 17 Fragen

1. **Vorhanden:** siehe §2. Client-UI, API, CVars, Engine 🟡; AC Opcodes und Stubs; Murmur vollständig; Referenzprojekte (Plugin, mumo, Ascent-Relay, CMaNGOS-PR).
2. **Direkt wiederverwendbar:**
   - Blizzard-UI, Lua-API-Semantik und CVars
   - `SMSG_FEATURE_SYSTEM_STATUS` (Voice-Flag), `CMSG_VOICE_SESSION_ENABLE`
   - Mumble.proto/MumbleUDP.proto
   - Koordinaten-Konvention aus wow3.dll
   - Client-Adressen aus WotLK-Extensions (MIT)
   - Bibliotheken: Opus, speexdsp, miniaudio
3. **Neu:** voice.dll (Mumble-Client-Stack, Audio, Renderer, Occlusion, WoW-Bridge, Lua-Overrides), Loader-Patch, AC-Modul (VoiceManager, MumbleBot, MVCP), Murmur-Konfiguration und ACL-Setup.
4. **Nutzbare Teile des Blizzard-Systems:** UI, API-Semantik, CVars, Keybinding, Opcodes zur Aktivierung und als Transport-Hülle. Die Engine und der Codec sind nicht nutzbar, siehe §4.
5. **Codec:** Opus, siehe §4.
6. **Opus-Integration:** libopus statisch in voice.dll, 20 ms / 48 kHz / VOIP / FEC, siehe §4.
7. **Murmur-Anbindung:** voice.dll ist ein vollwertiger Mumble-1.5-Client (TLS + Protobuf-UDP + OCB2). AC steuert über einen Bot per Mumble-Protokoll, ganz ohne Ice.
8. **Ohne wow3.dll:** voice.dll läuft **im** WoW-Prozess, liest Daten über Client-Funktionen und bringt den Mumble-Stack selbst mit. Es gibt kein Plugin-Interface und keinen fremden Prozess.
9. **AC-Modul:** siehe §6.2.
10. **Isolation:** Murmur-Channel pro Map-Instanz, serverseitig vom Bot gesetzt, ergänzt durch den Kontext-Filter, siehe §3.4.
11. **Positional Audio:** im Client aus Objektmanager-Positionen, Kamera-Orientierung, Distanzkurve und Panning, siehe §3.3.
12. **Occlusion:** Client-Raycasts, Glättung, Dämpfung und Tiefpass, siehe §3.7.
13. **Client-Änderungen:**
    - Wow.exe-Patch, der `voice.dll` lädt (wie AwesomeWotlk/WotLK-Extensions)
    - Hooks: `LoadFunctions` (Lua), `SetMessageHandler`/`ProcessMessage` (MVCP), Per-Frame-Hook, optional Unterdrücken der Comsat-Initialisierung ❓
14. **AC-Änderungen:** keine. Die Voice-Stubs des Cores bleiben unberührt, das Modul hakt sich per `CanPacketReceive` davor. Die `STATUS_NEVER`-Opcodes (Silence, Voice-Ignore) werden nicht gebraucht; Moderation läuft über MVCP und GM-Befehle.
15. **Eigenständiges Modul:** alles Serverseitige. Client, Loader und Murmur-Konfiguration liegen unter `external/`.
16. **RE-Risiken:**
    - Adressen gelten nur für 12340
    - Comsat-Seiteneffekte bei `EnableVoiceChat=1` ❓
    - Opcode-Handler-Übernahme ❓
    - TraceLine-Signatur und -Flags ❓
    - Warden
    - Abstürze bei fehlerhaften Hooks (Absicherung über SEH)
    - Blockierte Quellen (wowdev.wiki, warcraft.wiki.gg, wowpedia und das Warmane-Forum waren aus der Recherche-Umgebung nicht erreichbar)
17. **Belegt vs. Hypothese:** siehe Markierungen ✅/🟡/❓. Die zentralen Hypothesen sind in §7 gesammelt.

---

## 6. Implementierungsplan

### 6.1 Repository-Struktur

```text
mod-voicechat/                     ← direkt als modules/mod-voicechat klonbar
├── src/                           ← NUR das kompiliert AzerothCore
│   ├── mod_voicechat_loader.cpp   (Addmod_voicechatScripts)
│   ├── VoiceManager.{h,cpp}       Rechte, Bindings, Kontext, Queue
│   ├── MumbleBot.{h,cpp}          Asio/TLS-Client, Channels, Move/Mute/Kick
│   ├── VoiceScripts.cpp           Player-/Map-/Group-/Server-/World-Hooks
│   └── shared/                    portabler Code auch für den Client (Protobuf-Mini-Codec, MVCP, Mumble-Framing)
├── conf/mod_voicechat.conf.dist
├── data/sql/db-characters/        (nur falls Persistenz nötig, z. B. Voice-Mutes)
├── external/
│   ├── client/                    voice.dll, Loader-Patcher, Test-CLI, 3rd-party
│   └── murmur/                    mumble-server.ini-Vorlage, ACL-/Bot-Setup, docker-compose
└── docs/{de,en}/
```

### 6.2 Phasen

| Phase | Inhalt | Ergebnis / Test |
|---|---|---|
| **3 – PoC** | `voicecore` (C++17, portabel): TLS, Mumble-Handshake, CryptSetup/OCB2, Protobuf-UDP, Ping, UDP-Tunnel-Fallback, Opus, miniaudio. **Test-CLI** (Linux/Windows) | 2 CLI-Clients sprechen über Murmur. Automatisierter Test im Container gegen echten Murmur |
| | voice.dll-Grundgerüst + Loader: startet mit WoW, verbindet mit `voice.ini`, PTT über CVar `PushToTalkButton`, Logout trennt | WoW A ↔ Murmur ↔ WoW B (Test durch dich) |
| **4 – Positional** | Snapshot im Hauptthread, Objektmanager-Mapping, Distanz und Panning, VoiceTargets (nahe Sessions), Kontext | Lautstärke und Richtung ändern sich beim Laufen |
| **5 – AC** | Modul: Konfig, MVCP, Bot, Nonce-Binding, Channels pro Map/Instanz, Login/Logout/Map-/Instanzwechsel, Rechte und Flags, GM-Befehle (`.voice status/mute/kick`) | Kein Mithören über Instanzgrenzen (Test-Checkliste) |
| **6 – Occlusion** | TraceLine-Integration, Strahlbudget, Glättung, Tiefpass, konfigurierbare Faktoren | Wand dämpft hörbar |
| **7 – Native UI** | Lua-API-Overrides für die Blizzard-Voice-UI, Voice-Flag in `SMSG_FEATURE_SYSTEM_STATUS`, Sprecheranzeige, Mute-Liste, Geräteauswahl, Absenkung der Spielgeräusche | Fühlt sich an wie original WoW-Voice |
| 8 – Extras | Party-„Funk“ (zweites PTT, ohne Positionen), Schreien/Flüstern (Reichweite), HRTF, Moderation | – |

### 6.3 Was ich von dir brauche

- **Tests am Client** ab Phase 3b: Ich kann hier bauen und gegen Murmur testen, aber nicht WoW ausführen.
- **Verifikation der ❓-Adressen** (Checkliste kommt mit Phase 3b; optional x32dbg/IDA).
- **MPQ-Extrakte:** vorerst nicht nötig, die FrameXML ist öffentlich.
- ~~Entscheidung Loader~~: erledigt, `external/client/loader/`: 29 Byte in einer int3-Lücke, Dateigröße und PE-Header unverändert, kompatibel mit St0ny's Patcher.

---

## 7. Offene Hypothesen (zu verifizieren)

| # | Hypothese | Prüfung |
|---|---|---|
| H1 | Der Comsat-Codec ist nicht Opus-kompatibel | irrelevant für das Design; optional Strings-Analyse von Wow.exe |
| H2 | `EnableVoiceChat=1` ohne Roster startet keine störende Aufnahme | Test am Client; notfalls Comsat-Init hooken |
| H3 | Die Handler-Übernahme eines Voice-SMSG über `SetMessageHandler` (0x631FA0) ist stabil | PoC in Phase 5 |
| H4 | `TraceLine` bei 0x7A3B70 mit Terrain-/WMO-/M2-Flags | Debugger-Test in Phase 6 |
| H5 | Map-ID-Adresse 0xBD088C vs. 0xAB63BC | Abgleich im Spiel (Kontinent vs. Instanz) |
| H6 | `LoadFunctions`-Hook erlaubt das Überschreiben der Blizzard-Voice-Lua-Funktionen nach /reload | Phase 7 |
| H7 | Das Blizzard-UI reagiert korrekt auf selbst ausgelöste `VOICE_*`-Events | Phase 7 |

## 8. Quellen

- ReynoldsCahoon/WotLK-Mumble-Positional-Voice (`plugin/wow3.cpp`, `mumo-module/wowrp.py`), Stand `0960c7e`
- azerothcore/azerothcore-wotlk `3d28fa7`; Issue #5063; TrinityCore Issue #15057
- SkyFire/ascent_classic `src/ascent-voicechat`, `src/ascent-world/VoiceChatHandler.cpp`, `Group.cpp`
- celguar/voicechat-server (+ Wiki mit Blizzard-Doku 2008); cmangos/mangos-tbc PR #668
- wowgaming/3.3.5-interface-files (FrameXML 3.3.5)
- mumble-voip/mumble `7bbd2c1` (`Mumble.proto`, `MumbleUDP.proto`, `murmur/Server.cpp`, `Messages.cpp`, `AudioReceiverBuffer.cpp`, `ACL.h`, `mumble-server.ini`)
- Alyst3r/Sylian1337 WotLK-Extensions (MIT), FrostAtom/awesome_wotlk (GPL-3.0, nur als Referenz)
- TeamSpeak Client-SDK-Doku (3D-Sound, `onCustom3dRolloffCalculationClientEvent`); miniaudio, Opus, speexdsp

## Anhang: TeamSpeak vs. Mumble (nur technisch)

| Kriterium | TeamSpeak SDK | Mumble/Murmur |
|---|---|---|
| 3D im Client | ja, inkl. Rolloff-Callback und Sample-Callbacks (Occlusion möglich) | nein, wir rendern selbst (ohnehin nötig für WoW-Occlusion) |
| Protokoll selbst implementierbar | nein (geschlossen), Hersteller-DLL muss in den Client | ja, offen und dokumentiert |
| Einbettung in GPL-Client | proprietäre Bibliothek in GPL-Code, Verbreitung problematisch | BSD, problemlos |
| Server | proprietär, Steuerung per ServerQuery | Open Source, Steuerung per Protokoll-Bot oder Ice |
| 32-Bit-Windows | aktuelle SDK-Verfügbarkeit ❓ | selbst gebaut |
| Fazit | kein technischer Vorteil, weil wir das Rendering ohnehin selbst machen | **gewählt** |
