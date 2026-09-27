# mod-voicechat – Client (voice.dll)

> ⚠️ **WARNING: This project is at a very early stage. Everything is still untested and unverified. It is NOT released for playing!**

🇩🇪 [Deutsche Version](README.md)

## Components

| Folder | Content |
|---|---|
| `voicecore/` | Mumble 1.5 client: TLS (mbedTLS), OCB2-AES128 (compatible with Murmur), UDP with TCP tunnel fallback, Opus, jitter buffer and mixer, PTT/VAD |
| `voice/` | `voice.dll` for Wow.exe 3.3.5a: audio via WASAPI (miniaudio), push-to-talk, automatic connect when entering the world |
| `tools/voicecli/` | headless test client (send and verify a tone) |
| `tests/e2e.sh` | end-to-end test against a real Murmur |
| `loader/` | exe patch that makes Wow.exe load `voice.dll` (file size unchanged) |

The shared protocol code (`src/shared/MumbleProtocol.h`, `src/shared/VoiceProtocol.h`) is used by both `voice.dll` and the AzerothCore module.

## Phase 6 status

**What works:**
- **Transmission:** microphone → Opus → Murmur → other clients → speakers.
- **Positional voice:** every voice packet carries the speaker's position. The listener mixes by distance (full up to `MinDistance`, silent from `MaxDistance`) and direction, with stereo panning and a duller sound from behind.
- **Server integration (`[Server] Mode=auto`):**
  - On entering the world, `voice.dll` asks the worldserver over the WoW connection.
  - If the server runs `mod-voicechat`, the Murmur address, name, channel (map/instance) and permissions come from there.
  - The Mumble session is bound to the character via a one-time nonce.
  - Without an answer (server without the module), everything runs standalone as in phase 4.
- **Party/raid:** members are always fully audible, even on other maps or in instances. Their position only sets the direction: never quieter, no rear damping, no occlusion. Members on another map sound centred.
- **Strangers:** they are only audible when the server reports them as "in range", and they get quieter with distance.
- **Occlusion:** walls, buildings and terrain between you and a stranger make their voice quieter and duller. Adjustable in `voice.ini [Occlusion]` (level and low-pass at full occlusion).
  - Each speaker gets 3 line-of-sight rays (centre, left, right) via WoW's own `TraceLine`. The share of blocked rays gives a soft transition at corners.
  - The rays run on the main thread with a fixed budget per frame and are refreshed about every 100 ms. Transitions are smoothed (~120 ms).
  - Group members are never affected.
- **Sending:** audio only goes to "near + group" (Mumble whisper). This saves bandwidth, and a manipulated client cannot force audio on anyone.
- **No code patch:** game data and packets are processed on the WoW main thread (subclassed WoW window). For the server packets, only a handler is put into WoW's handler table; other packets go to the original handler.

**Deliberately not included yet:**
- **Own PTT key:** the key is set in `voice.ini`; WoW key bindings and the Blizzard voice UI follow in phase 7.
- **Addresses only checked statically:** all client addresses are in `voice/WowApi.h`. They were **checked statically** against the original 12340 exe (disassembly: handler table `conn+0x53C`, call `cdecl(param, opcode, time, CDataStore*)`), but **not at runtime** yet.

**Tested here:**
- **Transmission:** voicecli → Murmur 1.5.517 → voicecli over UDP and over the TCP tunnel: 200/200 packets, 199 clean 440 Hz frames.
- **Positions through Murmur:** a speaker at 5 yd is audible. At 100 yd it is silent even though packets arrive. On another map it is silent because Murmur strips the position.
- **Bot + Murmur** (`external/module-tests`):
  - Binding via nonce works.
  - Players end up in the right channel.
  - Different channels do not hear each other.
  - Unbound users are kicked.
  - Whispering across channel borders (group) arrives.
- **Unit tests:** distance, panning, rotation, behind/front, occlusion formula, group mode (never quieter), coordinates and click-free volume ramps. Plus the occlusion tracker against a test wall: full, free, partial (1/3), ray budget, smoothing and cleanup.
- **Builds:** `voice.dll` builds cleanly as a 32-bit DLL, and the AzerothCore module compiles without warnings.
- **Nothing has been tested inside WoW itself.**

## Getting voice.dll

- **Use the MSVC build for testing** (Actions artifact or Visual Studio): only it catches access violations when touching WoW. If an address does not match, it writes a message to `voice.log` and disables the game integration instead of crashing WoW. MinGW builds lack this protection.
- **Without building:** in the repo under *Actions → client → latest run → Artifacts → `voice-win32`* you find `voice.dll`, `voicecli.exe`, the example `voice.ini` and the loader.
- **Build it yourself (Windows, Visual Studio 2022):**
  ```bat
  cmake -S external/client -B build -A Win32
  cmake --build build --config Release
  ```
- **Build it yourself (Linux, MinGW cross build):**
  ```sh
  cmake -S external/client -B build-win32 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw32.cmake -DCMAKE_BUILD_TYPE=Release
  cmake --build build-win32
  ```

## Installing into a test client

1. Patch Wow.exe with the loader, see [`loader/README.en.md`](loader/README.en.md).
2. Put `voice.dll` next to `Wow.exe`. `voice.ini` (a copy of `voice/voice.ini.example`) is optional.
3. **With the AzerothCore module:** nothing else is needed, `Mode=auto` fetches everything from the server (setup: see the main README).
   **Without the module:** set `Mode=standalone` and the Murmur server under `[Server]` in `voice.ini`.
4. Murmur needs `opusthreshold=0` so that Opus is active right away. This is the default in current versions.
5. Start WoW, log in and hold the push-to-talk key (default: CAPSLOCK).

## Test checklist (please report back `voice.log` and the worldserver log)

- [ ] Does WoW start normally, with and without `voice.dll`?
- [ ] Is `voice.dll started (phase 6)` in `voice.log`?
- [ ] After entering the world, is `ServerLink: SMSG handler installed` in the log?
- [ ] **With the module:** do these lines appear in the log, in order?
  - [ ] `server config: <host>:<port> as '<name>'`
  - [ ] `connected, session N`
  - [ ] `bound to the character`
  - [ ] `context wow335|<realm>|<map>|<instance>`

  The worldserver log should show `<name> bound to Mumble session N`.
- [ ] **Without the module** (`Mode=auto`): after about 15 s, does `no answer from mod-voicechat ... -> standalone` appear, and does the client then connect using `voice.ini`?
- [ ] Is `UDP active` in the log?
- [ ] **Strangers:** do two clients (not grouped) hear each other? Does the voice get quieter when walking away, and is it silent from about 40 yd? Does the voice come from the right side?
- [ ] **Group:** is a group member still fully audible at 200 yd and from the right direction? Can you also hear them when they are in an instance or on another continent?
- [ ] **Occlusion:** does a stranger behind a house wall or a hill become quieter and duller, and does the sound come back softly when they step out? Does a group member behind the wall stay unchanged? If `occlusion: access violation` shows up in the log, the `TraceLine` address does not match, so please report it.
- [ ] Does the context change when entering an instance, and do players in different instances of the same dungeon ID not hear each other (unless in the same group)?
- [ ] Do `.voice status`, `.voice mute <name>`, `.voice unmute <name>` and `.voice kick <name>` work?
- [ ] On logout, is the connection closed (`disconnect: left world`) and the microphone released?
- [ ] Does the client reconnect when Murmur or the worldserver restarts?

With `voicecli.exe --host <server> --user Test --send-tone 440 --seconds 10` you hear a test tone in the game without needing a second WoW. This only works in standalone mode, because with the module the client only plays voices the server reports.
