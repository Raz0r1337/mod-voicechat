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

## Phase 7 status

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
- **Blizzard voice UI** (when the server has `Voice.BlizzardUi = 1` and `voice.ini [Ui] NativeUi=1`):
  - **Voice options menu:** it appears under *Interface → Sound & Voice → Voice*. voice.dll unlocks it like the server does at login, silently: no event, and WoW's old voice engine is not started.
  - **Settings:** they come from the menu. "Enable voice chat" is the master switch, plus microphone on/off, push-to-talk or voice activation (with sensitivity), the push-to-talk key (also with modifiers or mouse buttons 3–5), the volumes for microphone and voice, and the **input and output device**. The device lists in the menu are the devices voice.dll actually uses (WASAPI). "Default" means the Windows default device or `InputDevice`/`OutputDevice` from `voice.ini`. A change takes effect immediately, without reconnecting.
  - **Lowering game sounds:** while someone is audibly talking, sound, music and ambience are softly lowered to the strength of the sliders in the voice menu (factor on your normal volume). This is crash-safe: the original values are stored in `voice.duck` beforehand and written back on the next start. Can be switched off with `voice.ini [Ui] DuckGameSound=0`.
  - **Speaker icons:** the original speaker icon flashes on the player frame and the party frames when you or a group member talks. Group members with voice get the voice icon. Technically, a Lua bridge extends `UnitIsTalking`/`GetVoiceStatus` and delivers `VOICE_START`, `VOICE_STOP` and `VOICE_STATUS_UPDATE` to every frame that registered them, addons included.
- **No code patch:** game data and packets are processed on the WoW main thread (subclassed WoW window). For the server packets, only a handler is put into WoW's handler table; other packets go to the original handler.

**Deliberately not included yet:**
- **Not from the WoW menu yet:** microphone test and the talker list at the top left (`VoiceChatTalkers`).
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
- **Lua bridge:** with Lua 5.1 as in WoW (`tests/nativeui_test.sh`), the tests check the overrides, event delivery (also after a failing handler), "no events without change" and adding the menu exactly once.
- **Unit tests:** distance, panning, rotation, behind/front, occlusion formula, group mode (never quieter), coordinates and click-free volume ramps. Plus the lowering of game sounds (fading, slider movement, logging out, crash) and the occlusion tracker against a test wall: full, free, partial (1/3), ray budget, smoothing and cleanup.
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
- [ ] Is `voice.dll started (phase 7)` in `voice.log`?
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
- [ ] **Blizzard UI:**
  - [ ] Does `NativeUi: Blizzard voice options unlocked` appear in the log, and is there a *Voice* entry under *Interface → Sound & Voice*?
  - [ ] Is voice off until "Enable voice chat" is ticked (log: `voice chat is off in the WoW options`)?
  - [ ] Does it connect after ticking it?
  - [ ] Do the push-to-talk key from the menu (log: `push-to-talk: WoW binding '…'`), voice activation and the volume sliders work?
  - [ ] Do the device menus show your microphones and speakers, and does voice.dll switch on selection (log: `audio devices changed in the voice menu`)?
  - [ ] Do sound, music and ambience get quieter while someone talks (sliders "sound/music/ambience" in the voice menu)? Do they return to your normal values afterwards? After a WoW crash while someone was talking: are the volumes right again on the next start (log: `ducking: restored game volumes`)?
  - [ ] Does the speaker icon flash on your own frame and on the party frame while talking?
  - [ ] Does this still work after `/reload`, without *Voice* showing up twice in the menu?
  - [ ] Does WoW's old voice engine start anyway? You would notice e.g. a "voice chat unavailable" message or the microphone being taken twice.
- [ ] **Occlusion:** does a stranger behind a house wall or a hill become quieter and duller, and does the sound come back softly when they step out? Does a group member behind the wall stay unchanged? If `occlusion: access violation` shows up in the log, the `TraceLine` address does not match, so please report it.
- [ ] Does the context change when entering an instance, and do players in different instances of the same dungeon ID not hear each other (unless in the same group)?
- [ ] Do `.voice status`, `.voice mute <name>`, `.voice unmute <name>` and `.voice kick <name>` work?
- [ ] On logout, is the connection closed (`disconnect: left world`) and the microphone released?
- [ ] Does the client reconnect when Murmur or the worldserver restarts?

With `voicecli.exe --host <server> --user Test --send-tone 440 --seconds 10` you hear a test tone in the game without needing a second WoW. This only works in standalone mode, because with the module the client only plays voices the server reports.
