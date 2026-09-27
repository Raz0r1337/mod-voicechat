# mod-voicechat

> ⚠️ **WARNING: This project is at a very early stage. Everything is still untested and unverified. It is NOT released for playing!**

🇩🇪 **[Deutsche Version](README.md)**

Native 3D proximity voice for **World of Warcraft 3.3.5a (build 12340)** and **AzerothCore**, using **Murmur** (Mumble server) as the voice backend.

> **Status: phases 3–7 built, awaiting the first test in WoW.** Positional voice with AzerothCore integration (permissions, map/instance, group), damping through walls (occlusion) and the original Blizzard voice UI.
> The full report is in [docs/en/REPORT.md](docs/en/REPORT.md).

## Goal

| Who | Needs |
|---|---|
| Player | only the modified `Wow.exe` + `voice.dll`, **no** Mumble, TeamSpeak or Discord client and **no** `wow3.dll` |
| Server | AzerothCore with this module + a self-hosted Murmur server |

- **Positional audio:** volume by distance, direction via stereo/3D, damping through walls and terrain (occlusion: quieter and duller, soft transition at corners)
- **Separate worlds:** continents and every instance (e.g. Naxxramas #42 and #43) are isolated from each other
- **Party/raid:** group members are always heard at full volume, even on other maps or in instances. Their position only sets the direction of the voice. Strangers are only heard within range, getting quieter with distance.
- **Integration:** feels like the original WoW voice. Voice options menu under *Interface → Sound & Voice → Voice* (on/off, microphone, push-to-talk key, voice activation, volumes, devices), plus microphone test, lowering game sounds, speaker icons on player and party frames and the talker list.

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

## Installation (phase 5, testing only)

1. **Set up Murmur:** see [`external/murmur/README.en.md`](external/murmur/README.en.md) (Linux package, Docker or Windows). Set a **SuperUser password** for the module (`mumble-server -ini /etc/mumble/mumble-server.ini -supw <password>`).
2. **AzerothCore module:**
   1. Clone it into `azerothcore-wotlk/modules/mod-voicechat`, re-run CMake and build the worldserver. No core patch is needed.
   2. Copy `mod_voicechat.conf.dist` to `mod_voicechat.conf` and set at least these values: `Voice.Enable = 1`, `Voice.Bot.Password` (the SuperUser password), `Voice.PublicHost` (the Murmur address as players reach it) and `Voice.ServerPassword` (if Murmur has one).
3. **Client:** patch `Wow.exe` with the loader and put `voice.dll` into the WoW folder, see [`external/client/README.en.md`](external/client/README.en.md) (step-by-step test included). With the module, `voice.ini` needs no server data, because address, name and channel come from the worldserver. Each player switches voice on in the game: *Interface → Sound & Voice → Voice → Enable voice chat* (can be turned off with `Voice.BlizzardUi = 0`, then `voice.ini` applies).

### How the integration works

1. `voice.dll` announces itself to the worldserver over the existing WoW connection (opcode `CMSG_VOICE_SESSION_ENABLE` with extra data).
2. The module checks permissions (`Voice.MinSecurity`, account flags, GM kick) and sends the Murmur address, the name and a one-time nonce.
3. The client connects to Murmur and sends the nonce to the bot. This binds the Mumble session firmly to the character. Mumble users without a binding are kicked.
4. On a map or instance change, the bot moves players into the matching channel (`WoW/<realm>/Map-<id>[/Inst-<id>]`).
5. Every second, each client receives the lists "strangers in range" and "group". It only talks to those players (Mumble whisper) and only plays their voices.

### GM commands

| Command | Effect |
|---|---|
| `.voice status` | Murmur connection, bound and unbound users |
| `.voice mute [name]` / `.voice unmute [name]` | mute a player in voice, or release them again |
| `.voice kick [name]` | cut the voice connection until the player logs in again |

Players muted in chat (`.mute`) are also muted in voice by default (`Voice.MuteChatMuted`).

## Roadmap

| Phase | Content | Status |
|---|---|---|
| 1 | Research | ✅ |
| 2 | Architecture | ✅ |
| 3 | Proof of concept: client A ↔ Murmur ↔ client B with Opus ([client docs](external/client/README.en.md)) | 🧪 built, awaiting test in WoW |
| 4 | Positional audio (X/Y/Z, orientation, distance, direction) | 🧪 built, awaiting test in WoW |
| 5 | AzerothCore integration (login/logout, map/instance, permissions, group) | 🧪 built, awaiting test in WoW |
| 6 | Occlusion (raycast, attenuation, low-pass) | 🧪 built, awaiting test in WoW |
| 7 | Native Blizzard voice UI | 🧪 built, awaiting test in WoW |

## References

- [ReynoldsCahoon/WotLK-Mumble-Positional-Voice](https://github.com/ReynoldsCahoon/WotLK-Mumble-Positional-Voice)
- [AzerothCore issue #5063](https://github.com/azerothcore/azerothcore-wotlk/issues/5063)
- [Mumble](https://github.com/mumble-voip/mumble)
- [celguar/voicechat-server](https://github.com/celguar/voicechat-server)
- [WotLK-Extensions](https://github.com/Sylian1337/WotLK-Extensions)

## License

[GNU General Public License v2.0](LICENSE), the same license as AzerothCore. As with AzerothCore, the source code is licensed "version 2 or later".
Bundled third-party libraries (Opus, speexdsp, miniaudio, Mumble protocol definitions) are BSD or MIT-0 licensed and compatible.
