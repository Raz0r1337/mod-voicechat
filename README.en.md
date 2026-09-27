# mod-voicechat

🇩🇪 **[Deutsche Version](README.md)**

Native 3D proximity voice for **World of Warcraft 3.3.5a (build 12340)** and **AzerothCore**, using **Murmur** (Mumble server) as the voice backend.

> **Status: phase 2 complete (research + architecture).** There is no runnable code yet.
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

## Repository layout (planned)

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

## Installation

Coming once the first proof of concept works (phase 3). The plan:
1. `git clone` into `azerothcore-wotlk/modules/mod-voicechat`, then CMake and build. The module is detected automatically.
2. Adjust `mod_voicechat.conf` (Murmur host, bot credentials, range).
3. Set up Murmur with the template from `external/murmur/`.
4. Patch `Wow.exe` and put `voice.dll` into the WoW folder.

## Roadmap

| Phase | Content | Status |
|---|---|---|
| 1 | Research | ✅ |
| 2 | Architecture | ✅ |
| 3 | Proof of concept: client A ↔ Murmur ↔ client B with Opus | ⏳ |
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
