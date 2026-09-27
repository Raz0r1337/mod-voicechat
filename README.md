# mod-voicechat

> ⚠️ **WARNUNG: Dieses Projekt befindet sich in einem sehr frühen Stadium. Alles ist noch ungetestet und ungeprüft. Es ist NICHT zum Spielen freigegeben!**

🇬🇧 **[English version](README.en.md)**

Natives 3D-Proximity-Voice für **World of Warcraft 3.3.5a (Build 12340)** und **AzerothCore**, mit **Murmur** (Mumble-Server) als Voice-Backend.

> **Status: Phase 3 (Proof of Concept) gebaut, wartet auf den ersten Test in WoW.** Spieler können sich per Sprache verständigen, noch ohne Positionen und ohne AzerothCore-Anbindung.
> Den vollständigen Bericht findest du unter [docs/de/BERICHT.md](docs/de/BERICHT.md).

## Ziel

| Wer | Braucht |
|---|---|
| Spieler | nur den modifizierten `Wow.exe` + `voice.dll`, **keinen** Mumble-, TeamSpeak- oder Discord-Client und **kein** `wow3.dll` |
| Server | AzerothCore mit diesem Modul + einen eigenen Murmur-Server |

- **Positional Audio:** Lautstärke nach Entfernung, Richtung per Stereo/3D, später Dämpfung durch Wände (Occlusion)
- **Getrennte Welten:** Kontinente und jede Instanz (z. B. Naxxramas #42 und #43) sind voneinander isoliert
- **Integration:** fühlt sich an wie das originale WoW-Voice (Blizzard-Voice-UI, Push-to-Talk, Optionen)

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

## Installation (Stand Phase 3, nur zum Testen)

1. **Murmur aufsetzen:** siehe [`external/murmur/README.md`](external/murmur/README.md) (Linux-Paket, Docker oder Windows).
2. **Client:** `Wow.exe` mit dem Loader patchen und `voice.dll` + `voice.ini` in den WoW-Ordner legen, siehe [`external/client/README.md`](external/client/README.md) (Schritt-für-Schritt-Test inklusive).
3. **AzerothCore-Modul:** Das Modul kann schon nach `azerothcore-wotlk/modules/mod-voicechat` geklont werden und wird erkannt, **hat in Phase 3 aber noch keine Funktion.** Rechte, Map/Instanz und Login/Logout übernimmt es ab Phase 5, dann kommt auch `mod_voicechat.conf` dazu.

## Roadmap

| Phase | Inhalt | Status |
|---|---|---|
| 1 | Recherche | ✅ |
| 2 | Architektur | ✅ |
| 3 | Proof of Concept: Client A ↔ Murmur ↔ Client B mit Opus ([Client-Doku](external/client/README.md)) | 🧪 gebaut, wartet auf Test in WoW |
| 4 | Positional Audio (X/Y/Z, Orientierung, Distanz, Richtung) | 🧪 gebaut, wartet auf Test in WoW |
| 5 | AzerothCore-Integration (Login/Logout, Map/Instanz, Rechte) | – |
| 6 | Occlusion (Raycast, Dämpfung, Tiefpass) | – |
| 7 | Native Blizzard-Voice-UI | – |

## Referenzen

- [ReynoldsCahoon/WotLK-Mumble-Positional-Voice](https://github.com/ReynoldsCahoon/WotLK-Mumble-Positional-Voice)
- [AzerothCore Issue #5063](https://github.com/azerothcore/azerothcore-wotlk/issues/5063)
- [Mumble](https://github.com/mumble-voip/mumble)
- [celguar/voicechat-server](https://github.com/celguar/voicechat-server)
- [WotLK-Extensions](https://github.com/Sylian1337/WotLK-Extensions)

## Lizenz

[GNU General Public License v2.0](LICENSE), dieselbe Lizenz wie AzerothCore. Der Quellcode steht, wie bei AzerothCore, unter „Version 2 oder später“.
Eingebundene Drittbibliotheken (Opus, speexdsp, miniaudio, Mumble-Protokolldefinitionen) stehen unter BSD- bzw. MIT-0-Lizenz und sind kompatibel.
