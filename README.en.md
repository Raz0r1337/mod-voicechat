# mod-voicechat

> ⚠️ **WARNING: This project is at a very early stage. Everything is still untested and unverified. It is NOT released for playing!**

🇩🇪 **[Deutsche Version](README.md)**

Native 3D proximity voice for **World of Warcraft 3.3.5a (build 12340)** and **AzerothCore**, using **Murmur** (Mumble server) as the voice backend.

> **Status: phase 3 (proof of concept) built, awaiting the first test in WoW.** Players can talk to each other by voice, still without positions and without AzerothCore integration.
> The full report is in [docs/en/REPORT.md](docs/en/REPORT.md).

## Goal

| Who | Needs |
|---|---|
| Player | only the modified `Wow.exe` + `voice.dll`, **no** Mumble, TeamSpeak or Discord client and **no** `wow3.dll` |
| Server | AzerothCore with this module + a self-hosted Murmur server |

- **Positional audio:** volume by distance, direction via stereo/3D, later damping through walls (occlusion)
- **Separate worlds:** continents and every instance (e.g. Naxxramas #42 and #43) are isolated from each other
- **Integration:** feels like the original WoW voice (Blizzard voice UI, push-to-talk, options)

## Architecture at a glance

```text
WoW.exe + voice.dll ── Opus / Mumble protocol (TLS + UDP) ──► Murmur ◄── bot ── AzerothCore + mod-voicechat
  └─ 3D audio, occlusion, position (in the client)            (routing, isolation)   (permissions, map/instance, login/logout)
```

- **Murmur** transports and isolates, one channel per map instance. Murmur does **no** 3D processing.
- **voice.dll** is a built-in Mumble client. It captures audio, encodes it with Opus and renders 3D audio and occlusion.
- **AzerothCore** connects to Murmur as a bot, checks permissions, binds Mumble sessions to characters and moves players on map and instance changes.
- The AzerothCore core needs **no changes**.

## Repository layout

```text
mod-voicechat/          ← clone into AzerothCore as modules/mod-voicechat
├── src/                AzerothCore module (only this folder is compiled by the core)
├── conf/               mod_voicechat.conf.dist
├── data/sql/           automatic DB updates (if needed)
├── external/
│   ├── client/         voice.dll, loader patcher, test client
│   └── murmur/         Murmur configuration, ACL setup
└── docs/{de,en}/       documentation
```

## Installation (phase 3, testing only)

1. **Set up Murmur:** see [`external/murmur/README.en.md`](external/murmur/README.en.md) (Linux package, Docker or Windows).
2. **Client:** patch `Wow.exe` with the loader and put `voice.dll` + `voice.ini` into the WoW folder, see [`external/client/README.en.md`](external/client/README.en.md) (step-by-step test included).
3. **AzerothCore module:** the module can already be cloned into `azerothcore-wotlk/modules/mod-voicechat` and is detected, **but has no function yet in phase 3.** It takes over permissions, map/instance and login/logout from phase 5 on, which is also when `mod_voicechat.conf` arrives.

## Roadmap

| Phase | Content | Status |
|---|---|---|
| 1 | Research | ✅ |
| 2 | Architecture | ✅ |
| 3 | Proof of concept: client A ↔ Murmur ↔ client B with Opus ([client docs](external/client/README.en.md)) | 🧪 built, awaiting test in WoW |
| 4 | Positional audio (X/Y/Z, orientation, distance, direction) | – |
| 5 | AzerothCore integration (login/logout, map/instance, permissions) | – |
| 6 | Occlusion (raycast, attenuation, low-pass) | – |
| 7 | Native Blizzard voice UI | – |

## References

- [ReynoldsCahoon/WotLK-Mumble-Positional-Voice](https://github.com/ReynoldsCahoon/WotLK-Mumble-Positional-Voice)
- [AzerothCore issue #5063](https://github.com/azerothcore/azerothcore-wotlk/issues/5063)
- [Mumble](https://github.com/mumble-voip/mumble)
- [celguar/voicechat-server](https://github.com/celguar/voicechat-server)
- [WotLK-Extensions](https://github.com/Sylian1337/WotLK-Extensions)

## License

[GNU General Public License v2.0](LICENSE), the same license as AzerothCore. As with AzerothCore, the source code is licensed "version 2 or later".
Bundled third-party libraries (Opus, speexdsp, miniaudio, Mumble protocol definitions) are BSD or MIT-0 licensed and compatible.
