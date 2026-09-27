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

The shared protocol code (`src/shared/MumbleProtocol.h`) will also be used by the AzerothCore module later.

## Phase 3 status

**What works:**
- microphone → Opus → Murmur → other clients → speakers
- connect when entering the world (name = character name), disconnect on logout
- reconnect with backoff

**Deliberately not included yet:**
- **No positions and no isolation:** everyone in the same Murmur channel hears each other (phase 4/5).
- **No AzerothCore integration:** server and password come from `voice.ini` (phase 5).
- **Own PTT key:** the key is set in `voice.ini`; the WoW key binding is not used yet (phase 7).
- **Unverified addresses:** in-world detection reads two addresses from `wow3.dll` (`0xBD0792`, `0xC79D18`). They are **not verified** and can be overridden in `voice.ini`.

**Tested here:**
- voicecli → Murmur 1.5.517 → voicecli over UDP and over the TCP tunnel: 200/200 packets, 199 clean 440 Hz frames.
- `voice.dll` builds cleanly as a 32-bit DLL and imports only system DLLs.
- **Nothing has been tested inside WoW itself.**

## Getting voice.dll

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
2. Put `voice.dll` and `voice.ini` (a copy of `voice/voice.ini.example`) next to `Wow.exe`.
3. Enter the Murmur server in `voice.ini`.
4. Murmur needs `opusthreshold=0` so that Opus is active right away. This is the default in current versions.
5. Start WoW, log in and hold the push-to-talk key (default: CAPSLOCK).

## Test checklist (please report back `voice.log`)

- [ ] Does WoW start normally, with and without `voice.dll`?
- [ ] Does `voice.log` contain `voice.dll started`?
- [ ] When entering the world, do you see `connecting as '<character name>'` and then `connected, session N`? If not, the `[Wow]` addresses are wrong. For testing, set `AutoConnectInWorld=0` and `Username=Test`.
- [ ] Does the log contain `UDP active`?
- [ ] Can two clients hear each other?
- [ ] Does logging out disconnect (`disconnect: left world`) and release the microphone?
- [ ] Does the client reconnect when Murmur restarts?

With `voicecli.exe --host <server> --user Test --send-tone 440 --seconds 10` you hear a test tone in game without needing a second WoW.
