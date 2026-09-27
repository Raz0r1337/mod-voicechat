# mod-voicechat

> ⚠️ **WARNUNG: Dieses Projekt befindet sich in einem sehr frühen Stadium. Alles ist noch ungetestet und ungeprüft. Es ist NICHT zum Spielen freigegeben!**

🇬🇧 **[English version](README.en.md)**

Natives 3D-Proximity-Voice für **World of Warcraft 3.3.5a (Build 12340)** und **AzerothCore**, mit **Murmur** (Mumble-Server) als Voice-Backend.

> **Status: Phasen 3–7 gebaut, warten auf den ersten Test in WoW.** Positional Voice mit AzerothCore-Anbindung (Rechte, Map/Instanz, Gruppe), Dämpfung durch Wände (Occlusion) und der originalen Blizzard-Voice-Oberfläche.
> Den vollständigen Bericht findest du unter [docs/de/BERICHT.md](docs/de/BERICHT.md).

## Ziel

| Wer | Braucht |
|---|---|
| Spieler | nur den modifizierten `Wow.exe` + `voice.dll`, **keinen** Mumble-, TeamSpeak- oder Discord-Client und **kein** `wow3.dll` |
| Server | AzerothCore mit diesem Modul + einen eigenen Murmur-Server |

- **Positional Audio:** Lautstärke nach Entfernung, Richtung per Stereo/3D, Dämpfung durch Wände und Gelände (Occlusion: leiser und dumpfer, weicher Übergang an Ecken)
- **Getrennte Welten:** Kontinente und jede Instanz (z. B. Naxxramas #42 und #43) sind voneinander isoliert
- **Gruppe/Raid:** Gruppenmitglieder hört man immer in voller Lautstärke, auch auf anderen Maps oder in Instanzen. Ihre Position bestimmt nur die Richtung der Stimme. Fremde hört man nur in Hörweite, leiser mit zunehmender Entfernung.
- **Integration:** fühlt sich an wie das originale WoW-Voice. Voice-Optionsmenü unter *Interface → Sound & Voice → Voice* (Ein/Aus, Mikrofon, Push-to-Talk-Taste, Sprachaktivierung, Lautstärken) und Sprecher-Symbole an Spieler- und Gruppenrahmen.

## Architektur in Kürze

```text
WoW.exe + voice.dll ── Opus / Mumble-Protokoll (TLS + UDP) ──► Murmur ◄── Bot ── AzerothCore + mod-voicechat
  └─ 3D-Audio, Occlusion, Position (im Client)                 (Routing, Isolation)   (Rechte, Map/Instanz, Login/Logout)
```

- **Murmur** transportiert und trennt, ein Channel pro Map-Instanz. 3D rechnet Murmur **nicht**.
- **voice.dll** ist ein eingebauter Mumble-Client. Er nimmt auf, kodiert mit Opus und rendert 3D-Audio und Occlusion.
- **AzerothCore** verbindet sich als Bot mit Murmur, prüft Rechte, bindet Mumble-Sessions an Charaktere und verschiebt Spieler bei Map- und Instanzwechsel.
- Am AzerothCore-Core sind **keine Änderungen** nötig.

## Repository-Struktur

```text
mod-voicechat/          ← als modules/mod-voicechat in AzerothCore klonen
├── src/                AzerothCore-Modul (nur dieser Ordner wird vom Core kompiliert)
├── conf/               mod_voicechat.conf.dist
├── data/sql/           automatische DB-Updates (falls nötig)
├── external/
│   ├── client/         voice.dll, Loader-Patcher, Test-Client
│   └── murmur/         Murmur-Konfiguration, ACL-Setup
└── docs/{de,en}/       Dokumentation
```

## Installation (Stand Phase 5, nur zum Testen)

1. **Murmur aufsetzen:** siehe [`external/murmur/README.md`](external/murmur/README.md) (Linux-Paket, Docker oder Windows). Für das Modul ein **SuperUser-Passwort** setzen (`mumble-server -ini /etc/mumble/mumble-server.ini -supw <passwort>`).
2. **AzerothCore-Modul:**
   1. Nach `azerothcore-wotlk/modules/mod-voicechat` klonen, CMake neu ausführen und den Worldserver bauen. Ein Core-Patch ist nicht nötig.
   2. `mod_voicechat.conf.dist` nach `mod_voicechat.conf` kopieren und mindestens diese Werte setzen: `Voice.Enable = 1`, `Voice.Bot.Password` (SuperUser-Passwort), `Voice.PublicHost` (die Murmur-Adresse, wie die Spieler sie erreichen) und `Voice.ServerPassword` (falls Murmur eins hat).
3. **Client:** `Wow.exe` mit dem Loader patchen und `voice.dll` in den WoW-Ordner legen, siehe [`external/client/README.md`](external/client/README.md) (Schritt-für-Schritt-Test inklusive). Mit dem Modul braucht `voice.ini` keine Serverdaten, denn Adresse, Name und Channel kommen vom Worldserver. Voice schaltet jeder Spieler im Spiel selbst ein: *Interface → Sound & Voice → Voice → Voice-Chat aktivieren* (abschaltbar mit `Voice.BlizzardUi = 0`, dann gilt `voice.ini`).

### So funktioniert die Anbindung

1. `voice.dll` meldet sich über die bestehende WoW-Verbindung beim Worldserver (Opcode `CMSG_VOICE_SESSION_ENABLE` mit Zusatzdaten).
2. Das Modul prüft die Rechte (`Voice.MinSecurity`, Account-Flags, GM-Kick) und schickt Murmur-Adresse, Namen und eine Einmal-Nonce.
3. Der Client verbindet sich mit Murmur und schickt die Nonce an den Bot. Damit ist die Mumble-Session fest an den Charakter gebunden. Mumble-Nutzer ohne Bindung werden gekickt.
4. Der Bot verschiebt Spieler bei Map- oder Instanzwechsel in den passenden Channel (`WoW/<Realm>/Map-<id>[/Inst-<id>]`).
5. Jede Sekunde bekommt jeder Client die Listen „Fremde in Hörweite“ und „Gruppe“. Er spricht nur zu diesen Spielern (Mumble-Whisper) und spielt nur deren Stimmen ab.

### GM-Befehle

| Befehl | Wirkung |
|---|---|
| `.voice status` | Murmur-Verbindung, gebundene und ungebundene Nutzer |
| `.voice mute [Name]` / `.voice unmute [Name]` | Spieler im Voice stumm schalten bzw. wieder freigeben |
| `.voice kick [Name]` | Voice-Verbindung trennen, bis der Spieler sich neu einloggt |

Wer im Chat stummgeschaltet ist (`.mute`), ist standardmäßig auch im Voice stumm (`Voice.MuteChatMuted`).

## Roadmap

| Phase | Inhalt | Status |
|---|---|---|
| 1 | Recherche | ✅ |
| 2 | Architektur | ✅ |
| 3 | Proof of Concept: Client A ↔ Murmur ↔ Client B mit Opus ([Client-Doku](external/client/README.md)) | 🧪 gebaut, wartet auf Test in WoW |
| 4 | Positional Audio (X/Y/Z, Orientierung, Distanz, Richtung) | 🧪 gebaut, wartet auf Test in WoW |
| 5 | AzerothCore-Integration (Login/Logout, Map/Instanz, Rechte, Gruppe) | 🧪 gebaut, wartet auf Test in WoW |
| 6 | Occlusion (Raycast, Dämpfung, Tiefpass) | 🧪 gebaut, wartet auf Test in WoW |
| 7 | Native Blizzard-Voice-UI | 🧪 gebaut, wartet auf Test in WoW |

## Referenzen

- [ReynoldsCahoon/WotLK-Mumble-Positional-Voice](https://github.com/ReynoldsCahoon/WotLK-Mumble-Positional-Voice)
- [AzerothCore Issue #5063](https://github.com/azerothcore/azerothcore-wotlk/issues/5063)
- [Mumble](https://github.com/mumble-voip/mumble)
- [celguar/voicechat-server](https://github.com/celguar/voicechat-server)
- [WotLK-Extensions](https://github.com/Sylian1337/WotLK-Extensions)

## Lizenz

[GNU General Public License v2.0](LICENSE), dieselbe Lizenz wie AzerothCore. Der Quellcode steht, wie bei AzerothCore, unter „Version 2 oder später“.
Eingebundene Drittbibliotheken (Opus, speexdsp, miniaudio, Mumble-Protokolldefinitionen) stehen unter BSD- bzw. MIT-0-Lizenz und sind kompatibel.
