# mod-voicechat – Research Report, Architecture and Implementation Plan

🇩🇪 [Deutsche Version](../de/BERICHT.md)

As of: 2026-09-27 · Target: WoW 3.3.5a (build 12340) + AzerothCore + Murmur, native 3D proximity voice without external voice software.

**Legend:** ✅ verified (source checked) · 🟡 strong evidence · ❓ hypothesis, must be verified on the client

---

## 1. Summary

- The 3.3.5a client contains the complete old Blizzard voice system, internally called **"Comsat"**: UI, Lua API, CVars, opcodes and a working engine (capture, codec, UDP transport). Other emulators brought it back to life in 2007 (Ascent) and 2024 (CMaNGOS PR, with a reviewer test on 3.3.5).
- **The old engine is still not usable for our goal.** It only supports party, raid, BG and custom-channel voice without positions, uses an unknown proprietary codec and its own UDP relay protocol. Murmur, by contrast, only accepts Opus.
- **What we can reuse is the surface:** Blizzard voice UI, Lua API, CVars and key binding (push-to-talk). We replace the engine behind it with our own in `voice.dll`.
- **Murmur does no 3D.** It only forwards positional data, and only between users with an identical context. The receiving client computes distance, direction, occlusion and filtering anyway. The sensible split is therefore a hybrid: **Murmur for transport, routing and isolation, the WoW client for all spatial audio rendering**.
- **Map/instance isolation must happen server-side.** The client does not know its instance ID; only AzerothCore does. AzerothCore connects as a privileged Mumble bot and moves users into channels named after the `map/instance` scheme.
- Auth works without an Ice authenticator. The binding between Mumble session and WoW character uses a one-time nonce: WoW connection → AC, Mumble TLS → bot.
- The AzerothCore module needs **no core changes**; everything runs inside the module. We don't use the core's voice stubs: the module intercepts the packets before them via a hook. Moderation (mute, silence, kick) runs through MVCP and GM commands instead of the locked `STATUS_NEVER` opcodes.

---

## 2. Inventory (phase 1)

### 2.1 WoW 3.3.5a client

| Finding | Status | Source |
|---|---|---|
| Complete voice UI (`VoiceChat.lua/xml`, "Voice" options panel, `VoiceChatTalkers` speaker list, minimap voice button, channel pullout, mute list, speaker icons on party/raid frames) | ✅ | 3.3.5 FrameXML (wowgaming/3.3.5-interface-files) |
| Lua API, e.g. `IsVoiceChatEnabled`, `IsVoiceChatAllowedByServer`, `VoiceIsDisabledByClient`, `GetNumVoiceSessions`, `GetVoiceSessionInfo`, `GetVoiceSessionMemberInfoBySessionID`, `GetVoiceStatus`, `UnitIsTalking`, `SetActiveVoiceChannelBySessionID`, `VoiceEnumerateCaptureDevices/OutputDevices`, `VoiceSelectCaptureDevice/OutputDevice`, `VoiceChat_StartCapture`/loopback test, `VoiceChat_GetCurrentMicrophoneSignalLevel`, `AddMute/DelMute/GetMuteStatus`, `SetSelfMuteState`, `ChannelSilenceVoice` | ✅ | FrameXML |
| Events: `VOICE_START/STOP`, `VOICE_STATUS_UPDATE`, `VOICE_CHAT_ENABLED_UPDATE`, `VOICE_PUSH_TO_TALK_START/STOP`, `VOICE_SELF_MUTE`, `VOICE_SESSIONS_UPDATE`, `VOICE_CHANNEL_STATUS_UPDATE`, `VOICE_LEFT_SESSION`, `VOICE_PLATE_START/STOP`, `MUTELIST_UPDATE`, `CHANNEL_VOICE_UPDATE` | ✅ | FrameXML |
| CVars: `EnableVoiceChat`, `EnableMicrophone`, `VoiceChatMode` (PTT/voice activation), `PushToTalkButton`, `PushToTalkSound`, `VoiceActivationSensitivity`, `OutboundChatVolume`, `InboundChatVolume`, `ChatSoundVolume/ChatMusicVolume/ChatAmbienceVolume` (game audio ducking), `VoiceChatSelfMute`, `Sound_VoiceChatInputDriverIndex`, `Sound_VoiceChatOutputDriverIndex` | ✅ | FrameXML |
| Engine name "Comsat"; disabled without SSE or when a second WoW instance is running | ✅ | comment in `AudioOptionsPanels.lua` |
| Voice options only appear when the server allows it (`IsVoiceChatAllowedByServer`, set via `SMSG_FEATURE_SYSTEM_STATUS`) | ✅ | FrameXML + AC `CharacterHandler.cpp` |
| The engine works in 3.3.5a: capture → codec → encryption (16-byte key, zero key accepted) → UDP to the server from the roster packet | 🟡 | Ascent 2007; CMaNGOS PR #668 (2024), reviewer "tested on WotLK 3.3.5" |
| Codec of the Comsat engine | ❓ unknown | Not publicly documented. Irrelevant for us (see §5) |
| Max. 5 simultaneous speakers, 1 active voice channel | ✅ | Blizzard documentation 2008 (web archive, quoted in the voicechat-server wiki) |

**UDP format of the old engine** (from Ascent's relay): byte 4 = user slot, bytes 5–6 = channel ID, 7-byte packet = registration, rest = encrypted payload. The relay decodes nothing; it only fans packets out to the other slots.

### 2.2 AzerothCore (at `3d28fa7`, 2026-09-27)

| Finding | Status |
|---|---|
| All voice opcodes defined (`Opcodes.h` 0x39E–0x3FC) | ✅ |
| Only 3 handlers, all stubs that skip bytes: `CMSG_VOICE_SESSION_ENABLE` (AUTHED), `CMSG_SET_ACTIVE_VOICE_CHANNEL` (AUTHED), `CMSG_CHANNEL_VOICE_ON` (LOGGEDIN) | ✅ |
| The other voice CMSGs (`CMSG_CHANNEL_SILENCE_VOICE`, `CMSG_ADD/DEL_VOICE_IGNORE`, `CMSG_CHANNEL_VOICE_OFF`, `CMSG_VOICE_SET_TALKER_MUTED_REQUEST`) are `STATUS_NEVER` and get dropped **before** the module hook | ✅ `WorldSession.cpp` |
| `SMSG_FEATURE_SYSTEM_STATUS` hard-codes "voice = 0" (two places in `CharacterHandler.cpp`) | ✅ |
| `ACCOUNT_FLAG_DISABLE_VOICE` / `_DISABLE_VOICE_SPEAK` exist but are not implemented | ✅ |
| Module hooks: `ServerScript::CanPacketReceive/CanPacketSend` (read and suppress packets), `OnPlayerLogin/Logout`, `AllMapScript::OnPlayerEnterAll/LeaveAll` (also on instance change), `GroupScript`, `OnPlayerJoinBG`, `WorldScript::OnUpdate/OnStartup` | ✅ |
| AC compiles only `modules/<module>/src/**`, copies `conf/*.conf.dist` and applies `data/sql/db-*` automatically | ✅ `ConfigureModules.cmake`, `UpdateFetcher.cpp` |
| The core rejects custom opcodes ≥ 0x521 (`IsValidOpcode`) | ✅ |

**Issue #5063** (ReynoldsCahoon, 2021) is a plain feature request with no technical content. It links TrinityCore #15057 (also no technical content) and **Ascent Classic `ascent-voicechat`**, the only real historic implementation.

### 2.3 Historic implementations

- **Ascent (2007, Burlex):** The world server connects over TCP to a separate voice relay (its own mini protocol: create/delete channel, activate/deactivate slot, ping). The world server sends the client `SMSG_VOICE_SESSION_ROSTER_UPDATE` with session ID, channel ID, type, name, 16-byte key, the relay's IPv4 address and port, and the member list (GUID, slot, flags).
- **celguar/voicechat-server + CMaNGOS mangos-tbc PR #668 (2024, open):** Built on Ascent; party, raid, BG, custom channels, mute and silence work. The full format of roster, `SMSG_AVAILABLE_VOICE_CHANNEL`, `SMSG_VOICE_SESSION_LEAVE`, `SMSG_VOICE_CHAT_STATUS` and `SMSG_VOICESESSION_FULL` is documented in that code. Proximity is not implemented.
- TrinityCore and MaNGOS mainline have no voice implementation.

### 2.4 ReynoldsCahoon/WotLK-Mumble-Positional-Voice

**`wow3.dll`** (`plugin/wow3.cpp`) is a classic Mumble positional plugin. It uses `peekProc` to read 9 fixed addresses from a **foreign** process:

| Value | Address | Note |
|---|---|---|
| In-game state | `0xBD0792` | 1 = in game |
| Position | `0xADF4E4` | also used as camera position |
| Heading | `0xBEBA70` | also used as camera direction |
| Camera front/top | `0xADF5F0` / `0xADF554` | |
| Character name | `0xC79D18` | |
| Map ID | `0xAB63BC` | WotLK-Extensions uses `0xBD088C` ❓ to clarify |
| Leader GUID | `0xBD1968` | read only as `int`, so truncated to 32 bits |

- **Coordinates:** Mumble X = −WoW Y, Mumble Y = WoW Z, Mumble Z = WoW X.
- **Context and identity:** context is `{"map": id}`, identity is `{"char": name, "leaderguid": n}`.

**`mumo-module/wowrp.py`** creates channels (Proximity Groups → Waiting Room, Overworld/continents, Group Channels) and moves users by map ID. Anyone in a dungeon lands in the channel of their leader GUID. Weaknesses:
- No authentication; the client can forge context and identity at will.
- No real instance separation; separation happens only via the leader GUID.
- `eval()` on config values.
- Cleanup bug: the channel is deleted before its entry is cleaned up.
- No protection against *Listen* permissions.

**Worth taking over:** the coordinate convention, the idea "context = world", and the channel hierarchy. **Not taken over:** reading a foreign process, trusting client data, and Python/Ice.

### 2.5 Murmur (Mumble server, source `7bbd2c1`)

| Finding | Status |
|---|---|
| Transport: TLS TCP (protobuf `Mumble.proto`) + UDP voice (protobuf `MumbleUDP.proto` since 1.5, legacy format before), encrypted with **OCB2-AES128**, fallback UDP over TCP (`UDPTunnel`) | ✅ |
| Opus mandatory: in Opus mode the server drops every non-Opus packet | ✅ `Server.cpp` |
| **The server renders no 3D**: positional data is only forwarded if `sender.ssContext == receiver.ssContext`. No distance filtering | ✅ `AudioReceiverBuffer.cpp` |
| Routing: same channel + linked channels (transitive!) + channel listeners + VoiceTargets (whisper/shout to sessions/channels/groups) | ✅ |
| Whispering to sessions requires the `Whisper` permission **in the receiver's channel** | ✅ |
| The mover's `Move` permission overrides a missing `Enter` for the target | ✅ `Messages.cpp` |
| **`Listen` permission** lets users listen to foreign channels without entering them, so it must be denied | ✅ |
| Remote control only via ZeroC Ice (no more gRPC in the code) | ✅ |
| Protection: per-user bandwidth limit, `messagelimit/burst`, autoban, `users`, `channelcountlimit` (default 1000) | ✅ |

---

## 3. Architecture (phase 2)

### 3.1 Overview

```text
┌──────────────────────── WoW.exe (3.3.5a, patched: loads voice.dll) ───────────────────────┐
│ Blizzard voice UI (FrameXML) ⇄ Lua API overrides ⇄ voice.dll                              │
│   voice.dll:                                                                             │
│   ├─ WoW bridge (main thread): position/orientation, map, object manager (GUID→position), │
│   │   raycasts (occlusion), CVars, PTT, own control protocol MVCP over the WoW socket     │
│   ├─ Audio (own thread): miniaudio (WASAPI) → speexdsp (AGC/denoise/VAD) → Opus          │
│   ├─ Mumble client: TLS (control) + UDP/OCB2 (voice), VoiceTargets, context               │
│   └─ Renderer: per speaker jitter buffer → Opus decoder → distance · direction ·          │
│      occlusion (low-pass) → stereo/HRTF mix → output                                      │
└──────────────┬───────────────────────────────────────────────────────┬───────────────────┘
               │ WoW protocol (MVCP inside voice opcodes)               │ Mumble (TLS 64738 + UDP)
               ▼                                                        ▼
┌──── AzerothCore worldserver + mod-voicechat ────┐         ┌──────── Murmur ─────────┐
│ VoiceManager: permissions, session binding,     │ Mumble  │ channels = map/instance │
│ context                                         │◄──TLS──►│ ACL: no Enter/Listen,   │
│ MumbleBot (Asio thread): create channels,       │         │ Whisper only "in"       │
│ move/mute/kick users                            │         │                         │
└─────────────────────────────────────────────────┘         └─────────────────────────┘
```

### 3.2 Variant A vs. B: result

| | A: "Mumble does 3D" | B: "client does everything" | **Chosen: hybrid** |
|---|---|---|---|
| Who renders distance and direction? | In Mumble the **client** (not Murmur!), so without a Mumble client nobody does | voice.dll | voice.dll |
| Isolation (map/instance) | context is client-set and therefore forgeable | the client would have to enforce it itself | **Murmur channels, set server-side by AC** |
| Bandwidth | everyone in the channel hears everyone | the client decides on its own | **VoiceTarget = list of nearby sessions** (client), whisper ACL limited to the user's own channel |
| Occlusion | impossible | raycast in the client | raycast in the client |

Variant A assumes a Mumble client that does the rendering. Since there is none, that job necessarily falls to voice.dll. Murmur contributes secure routing, channels, ACL and the positional data in each packet.

### 3.3 Positions

- **The primary source is the client's object manager** (Mumble session → character GUID → unit position). It cannot be spoofed (the positions come from the server), it is smooth (same interpolation as the model), and it costs no protocol.
- **The fallback** is `positional_data` in the Opus packet (Mumble convention: metres, Y up). It covers players outside visibility range who are still reachable in the channel via VoiceTarget, and debugging with desktop Mumble.
- **Listener:** position = character, orientation = camera yaw. Switchable to the character's facing. ❓ A test will decide which feels more natural.
- **Curve:** `gain = clamp(1 − (d − d_min)/(d_max − d_min))^k`. Defaults: `d_min` = 3 yd, `d_max` = 40 yd (WoW say range: 25 yd, yell: 100 yd), configurable per server and distributed to clients via MVCP.
- **Direction:** equal-power panning first, optional HRTF later.

### 3.4 Map/instance isolation

- **Channel tree in Murmur** (managed by the bot):
  - `WoW/<realm>/Map-<id>` for continents, persistent
  - `WoW/<realm>/Map-<id>/Inst-<instanceId>` for instances, BGs and arenas, as a **temporary channel** (Murmur deletes empty channels automatically)
  - optional suffix `-A`/`-H` when `Voice.CrossFaction = 0`
- **Context string** (for Murmur's positional filter): `wow|<realm>|<map>|<instance>`. The client sets it, but only to the value the server dictates via MVCP. Real security still comes from the channel.
- **ACL template:**
  - Root denies `@all` the permissions Enter, Listen, MakeChannel, MakeTempChannel, LinkChannel, Whisper, TextMessage and Register.
  - The bot gets Move, MuteDeafen, Kick, MakeChannel and Write.
  - Every instance channel allows `@in` the permissions Speak and Whisper.
  - The lobby channel allows TextMessage only to the bot.
- **Phasing** (`PhaseMask`) is possible as an optional additional context level. Off by default, because phases change often.

### 3.5 Authentication and lifecycle

```text
Login  : AC checks permission (config, account flag DISABLE_VOICE, GM level)
         Client → AC   MVCP HELLO(version)                 [voice.dll only]
         AC → Client   MVCP CONFIG(host, port, srvpw?, nonce N (128-bit, 60 s, single use), context, range)
         Client → Murmur  TLS + Authenticate(name = character name, own client certificate)
         Client → Bot     TextMessage "BIND N"   (bot compares in constant time → binds session↔GUID)
         Bot → Murmur     UserState(move → map/instance channel), on DISABLE_VOICE_SPEAK: mute
Map/instance change : OnPlayerEnterAll → bot moves; AC → client MVCP CONTEXT(seq, context)
Logout / disconnect : bot kicks the session; the client also disconnects itself
Unbound session     : kicked after 15 s
Bot reconnect       : backoff; afterwards a new nonce for every online player (rebind)
Murmur gone         : client backoff reconnect; UI shows status (VOICE_STATUS_UPDATE)
```

- voice.dll generates the certificate itself on first start (self-signed).
- The nonce never travels through Lua, so addons can't see it. The server only sends MVCP after a HELLO, so clients without voice.dll never receive unknown payloads.

### 3.6 MVCP: control channel without core changes

- **Client → server:** `CMSG_VOICE_SESSION_ENABLE` (0x3AF). The first 2 bytes keep their original meaning, followed optionally by `'MVC1'` + a TLV message.
  - The module reads the packet in `CanPacketReceive` and suppresses the stub handler.
  - The stock client sends this opcode itself (2 bytes) when voice settings change. The module treats that as EnableVoice/EnableMic.
- **Server → client:** a voice SMSG whose handler voice.dll replaces (`CNetClient::SetMessageHandler`). The candidate is `SMSG_VOICE_SESSION_ADJUST_PRIORITY` (0x3A0) ❓. Its original handler then never runs.
- **Rate limit** per session in the module.
- **Debug/fallback alternative:** addon messages (whisper to self, prefix `MVC`). They need no client hooks at all, but they are visible to Lua and therefore to addons, so never use them for nonces.

### 3.7 Occlusion (phase 6)

- **Where:** in the client, because the WoW geometry (terrain, WMO, M2) is loaded there. Raycast via the client function `TraceLine` / `CWorld::Intersect` (address per community `0x7A3B70` ❓, flags for terrain/WMO/M2 ❓).
- **Rays:** per speaker, 1 direct head→head ray plus 2–4 offset rays. The fraction of clear rays gives partial occlusion.
- **Budget:** N rays per frame round-robin, results smoothed (~150 ms attack/release). Runs on the main thread only, because the engine is not thread-safe.
- **Formula:** `gain = distance(d) · occlusion(o)`. The mapping table (1.0 / 0.6 / 0.25 / 0.05) is configurable, plus a **low-pass** with `cutoff = lerp(20 kHz, 800 Hz, o)`.
- **Materials:** the collision flags distinguish terrain, WMO and M2. There are no real materials; WMO group or doodad IDs would be a later approach ❓.
- **Server-side** (AC VMAP `isInLineOfSight`) would also work, but with latency and server load. Only worth it as an anti-cheat option.

**Phase 6 implementation:**
- **Signature (disassembly):** `bool __cdecl TraceLine(start*, end*, hit* (optional), float* fraction, flags, 0)`.
  - `fraction` goes in as 1.0 and comes out as the hit share: `hit = start + (end − start) · fraction`.
  - WoW's only direct call (0x77F550) uses the flags `0x100111` (terrain + WMO + models + movable objects). That is our default, adjustable in `voice.ini`.
- **Rays:** 3 per speaker, head to head (head height 1.5 yd): centre and ±0.7 yd side offsets. Occlusion = share of blocked rays.
- **Budget and smoothing:**
  - 9 rays per frame, most overdue speakers first, each about every 100 ms.
  - Smoothing with a 120 ms time constant. The first result applies immediately, so a new speaker behind a wall is not loud at first.
- **Effect:** `level · (1 − o · (1 − Gain))` with `Gain` = 0.35 and a low-pass down to 1 kHz at full occlusion.
  - It only affects strangers who are talking and in range.
  - The group is never affected.
- **Crash protection:** the calls run on the main thread with SEH protection (MSVC). On an access violation, occlusion is switched off and logged.
- **Tests:** `OcclusionTracker` lives in voicecore and is tested against a test wall (`tests/occlusion_test.cpp`).

### 3.8 Threading and robustness

- **Client:**
  - The main thread (hook on a per-frame function or a Lua OnUpdate frame) writes a snapshot (listener, speaker positions, occlusion) into a double buffer.
  - Audio and network threads **never** call WoW functions.
  - All hooks run inside SEH/try; on failure voice.dll disables itself and the game keeps running.
- **AC:**
  - The bot runs in its own `boost::asio` thread (Asio and OpenSSL are already available in AC).
  - Game events reach the bot through a thread-safe queue (AC updates maps in parallel!). `WorldScript::OnUpdate` processes the replies.
  - Players are only looked up by GUID (`ObjectAccessor`), never kept as raw pointers across threads.
  - Race protection: sequence numbers per context change; the bot only processes the latest state per GUID.

### 3.9 Security

| Risk | Measure |
|---|---|
| Eavesdropping on foreign instances | one channel per instance, no Enter/Listen for users, Whisper only `@in`, context dictated by the server |
| Identity hijacking | nonce (128-bit, 60 s, single use, constant-time compare) over the already authenticated WoW channel; Mumble name = character name; duplicates rejected |
| Voice spam | Murmur bandwidth limit, capped Opus bitrate, GM mute → Murmur `mute`, `ACCOUNT_FLAG_DISABLE_VOICE(_SPEAK)` enforced |
| DDoS | Murmur autoban, `messagelimit`, `users`; protect the UDP port with firewall/rate limiting; unbound sessions get kicked |
| Encryption | TLS 1.2+ for control; OCB2-AES128 for UDP (OCB2 has published weaknesses, Mumble includes countermeasures, current Mumble state of the art) |
| Bot access | dedicated registered bot user with minimal permissions instead of SuperUser; credentials only in `mod_voicechat.conf` |
| Warden | our patches/hooks can trigger AC Warden checks. Configure Warden checks on your own server accordingly |

---

### 3.10 Phase 5 implementation: deviations from the plan

| Plan | Implemented | Reason |
|---|---|---|
| Instance channels as temporary channels | **permanent** channels cleaned up by the bot (`Voice.Bot.EmptyChannelTimeout`) | Murmur pulls the creator into a temporary channel, so the bot itself would be moved |
| Root denies Whisper | **Whisper allowed** in the `WoW/` tree, speaking only `@in`. The client only plays voices that the server reports in NEARBY | **Party/raid is audible across maps** (see below). A manipulated client still cannot force audio on anyone |
| Kick unbound sessions after 15 s | `Voice.BindTimeoutSeconds` (default 30 s), exceptions via `Voice.AllowedExternalUsers` | time to set up UDP and TLS |
| Client certificate | none yet; binding uses only the one-time nonce (128 bit, 60 s) | enough for the mapping, a certificate can be added later |
| Original 0x3A0 handler never runs again | packets without `MVC1` go to the original handler | less intrusive |
| Context `wow\|realm\|map\|instance` | `wow335\|<realm>\|<map>\|<instance>[\|A/H]` | build in the context |

**Group rule** (a project requirement):
- **Party and raid members** are always heard at full volume, even on other maps or in instances. The position only sets the direction (panning): no distance attenuation, no rear damping, no occlusion. Without a position (another map), the voice sounds centred.
- **Strangers** are only heard when the server reports them as "in range", and they get quieter with distance. In instances you are normally only with your own group, so the distance rule effectively applies only in the open world.
- **Sending:** the client whispers to "near ∪ group" (VoiceTarget 1). The server sends both lists every second (`NEARBY`: field 1 = near, field 2 = group), and only when they have changed.

**H3 (handler takeover):** the disassembly confirms:
- The table is at `NetClient+0x53C`, the parameters at `+0x19B8`.
- Dispatcher 0x631FE0 calls `cdecl(param, opcode, time, CDataStore*)`.
- `SetMessageHandler` and `SendPacket` abort with a fatal error when there is no connection, so voice.dll checks `[0xC79CF4]` first.

The runtime test is still pending.

### 3.11 Phase 7 implementation: Blizzard voice UI

**Findings (disassembly 12340):**

| What | Result |
|---|---|
| `IsVoiceChatAllowedByServer()` (0x4FCCB0) | only reads the flag `[0xBCF004]` |
| `IsVoiceChatEnabled()` (0x4FCBF0) | requires the CVar `EnableVoiceChat` (int at `+0x30`) and the server flag, plus `!VoiceIsDisabledByClient` |
| `SMSG_FEATURE_SYSTEM_STATUS` (0x3C9) | sets `[0xBCF004]` silently, without an event. **WoW removes this handler when entering the world** (0x6B0BC0 = `ClearMessageHandler`), so the packet is ignored in game. |
| `SMSG_VOICE_CHAT_STATUS` (0x3E3, handler 0x500240) | toggles the flag at runtime, fires a UI event and **starts WoW's own voice engine** (0x9868C0) once `EnableVoiceChat` and `EnableMicrophone` are set. That is why we do not use it. |
| Lua `GetCVar` (0x510040) | calls `CVar::Lookup(name)` (0x767460, cdecl). The value string is at `+0x28`, bit 6 of `+0x1C` means "protected". |
| `FrameScript_Execute` (0x819210) | `(code, chunkName, taint)`. WoW itself passes `0` for secure system code. |

**Implementation:**
- **Unlocking:**
  - The server allows the UI via CONFIG field 11 (`Voice.BlizzardUi`).
  - voice.dll then sets `[0xBCF004] = 1` on the main thread, exactly like the login packet, silently.
  - Players without voice.dll see nothing.
  - If Blizzard's `PLAYER_ENTERING_WORLD` code has already run, the Lua bridge adds the menu once (duplicate check via `AudioOptionsFrame.categoryList`) and initialises it like Blizzard does (`BlizzardOptionsPanel_OnEvent`).
- **Settings:** voice.dll reads the CVars every 250 ms via `CVar::Lookup`. `EnableVoiceChat` = 0 disconnects; switching it back on triggers a fresh HELLO/CONFIG.
- **Speaker icons:**
  - The Lua bridge runs via `FrameScript_Execute` with taint 0, idempotently every 5 s or immediately on changes.
  - It extends `UnitIsTalking` and `GetVoiceStatus`.
  - `VOICE_START`, `VOICE_STOP` and `VOICE_STATUS_UPDATE` go to all frames via `EnumerateFrames` + `IsEventRegistered`, each in `pcall` and with the legacy globals `this`/`event`/`arg1`.
- **Crash protection and tests:** all calls are under the SEH guard. The Lua bridge is tested with Lua 5.1 and mocked WoW functions.
- **Device selection (phase 8a):** the Lua bridge replaces `Sound_ChatSystem_GetNum/…DriverNameByIndex` and `VoiceEnumerate/Select*Device` with voice.dll's WASAPI device list (index 0 = default). voice.dll reads `Sound_VoiceChatInput/OutputDriverIndex` and reopens the devices on change.
- **Lowering game sounds (phase 8b):** `CVar::Set` (0x7668C0, thiscall with 5 arguments, like Lua `SetCVar` at 0x514CD2) fades `Sound_SFX/Music/AmbienceVolume` to "original · factor" (sliders `ChatSound/Music/AmbienceVolume`). The original values are stored in `voice.duck` beforehand; a crash is repaired on the next start (also at the login screen). The logic lives, testable, in `voicecore::Ducking`.
- **Microphone test (phase 8c):**
  - Via `FrameScript_RegisterFunction` (0x817F90: `pushcclosure`, `pushstring`, `insert`, `rawset` into the globals), voice.dll registers its own C functions under Blizzard's names: `VoiceChat_Record/StopRecording/Play/StopPlayingLoopbackSound`, `VoiceChat_Is{Recording,Playing}LoopbackSound` (returns 0/1 as a number, as the menu expects) and `VoiceChat_GetCurrentMicrophoneSignalLevel` (0–100).
  - Lua C API: `lua_gettop` 0x84DBD0, `lua_tonumber` 0x84E030, `lua_pushnumber` 0x84E2A0.
  - The logic lives in `voicecore::LoopbackTest`.
- **Open:** the talker list `VoiceChatTalkers` (needs the session API).

## 4. Codec

1. **Which codec Comsat uses** is ❓ unknown. No reachable source documents it; static analysis (strings/signatures in Wow.exe) could determine it.
2. **It is present** 🟡, since the engine works in 3.3.5a.
3. **It is not usable with Mumble:** Murmur only accepts Opus, and the payload is proprietary. Transcoding on the server would be pointless.
4. **Pipeline** (from FrameXML): device selection → mic level/loopback test → PTT or voice activation (threshold) → codec → UDP relay → playback with game audio ducking.
5. **Reuse Comsat's capture and playback?** Not worthwhile: it needs deep reverse engineering of the engine internals, and the gain is small, because miniaudio does the job in a few hundred lines.
6. **Replace only the codec or the transport?** Theoretically possible via hooks, but with high RE risk and still without positions. We replace the **whole engine** and keep **UI, API and CVars**.

**Opus integration:**
- **libopus** (BSD) linked statically for Win32 (MSVC/MinGW), 48 kHz mono, VOIP application mode, 20 ms frames, 24–40 kbit/s, in-band FEC and DTX on, packet loss concealment (PLC) when decoding.
- **speexdsp** (BSD) for noise suppression, AGC, VAD, optional echo cancellation, jitter buffer and resampler.
- **miniaudio** (MIT-0) for WASAPI capture and playback, device enumeration and low-pass filtering.

All licenses are GPL-2.0 compatible.

---

## 5. Answers to the 17 questions

1. **Present:** see §2. Client UI, API, CVars, engine 🟡; AC opcodes and stubs; Murmur complete; reference projects (plugin, mumo, Ascent relay, CMaNGOS PR).
2. **Directly reusable:**
   - Blizzard UI, Lua API semantics and CVars
   - `SMSG_FEATURE_SYSTEM_STATUS` (voice flag), `CMSG_VOICE_SESSION_ENABLE`
   - Mumble.proto/MumbleUDP.proto
   - coordinate convention from wow3.dll
   - client addresses from WotLK-Extensions (MIT)
   - libraries: Opus, speexdsp, miniaudio
3. **New:** voice.dll (Mumble client stack, audio, renderer, occlusion, WoW bridge, Lua overrides), loader patch, AC module (VoiceManager, MumbleBot, MVCP), Murmur configuration and ACL setup.
4. **Usable parts of the Blizzard system:** UI, API semantics, CVars, key binding, opcodes for enabling voice and as a transport envelope. The engine and codec are not usable, see §4.
5. **Codec:** Opus, see §4.
6. **Opus integration:** libopus statically in voice.dll, 20 ms / 48 kHz / VOIP / FEC, see §4.
7. **Murmur connection:** voice.dll is a full Mumble 1.5 client (TLS + protobuf UDP + OCB2). AC controls Murmur through a bot over the Mumble protocol, with no Ice at all.
8. **Without wow3.dll:** voice.dll runs **inside** the WoW process, reads data through client functions and brings its own Mumble stack. There is no plugin interface and no foreign process.
9. **AC module:** see §6.2.
10. **Isolation:** one Murmur channel per map instance, set server-side by the bot, complemented by the context filter, see §3.4.
11. **Positional audio:** in the client from object-manager positions, camera orientation, distance curve and panning, see §3.3.
12. **Occlusion:** client raycasts, smoothing, attenuation and low-pass, see §3.7.
13. **Client changes:**
    - a Wow.exe patch that loads `voice.dll` (like AwesomeWotlk/WotLK-Extensions)
    - hooks: `LoadFunctions` (Lua), `SetMessageHandler`/`ProcessMessage` (MVCP), a per-frame hook, optionally suppressing Comsat initialization ❓
14. **AC changes:** none. The core's voice stubs stay untouched; the module hooks in before them via `CanPacketReceive`. The `STATUS_NEVER` opcodes (silence, voice ignore) are not needed; moderation runs through MVCP and GM commands.
15. **Stand-alone module:** everything server-side. Client, loader and Murmur configuration live under `external/`.
16. **RE risks:**
    - addresses are valid for 12340 only
    - Comsat side effects with `EnableVoiceChat=1` ❓
    - opcode handler takeover ❓
    - TraceLine signature and flags ❓
    - Warden
    - crashes from faulty hooks (guarded by SEH)
    - blocked sources (wowdev.wiki, warcraft.wiki.gg, wowpedia and the Warmane forum were not reachable from the research environment)
17. **Verified vs. hypothesis:** see the ✅/🟡/❓ markers. The central hypotheses are collected in §7.

---

## 6. Implementation plan

### 6.1 Repository layout

```text
mod-voicechat/                     ← clone directly as modules/mod-voicechat
├── src/                           ← the ONLY part AzerothCore compiles
│   ├── mod_voicechat_loader.cpp   (Addmod_voicechatScripts)
│   ├── VoiceManager.{h,cpp}       permissions, bindings, context, queue
│   ├── MumbleBot.{h,cpp}          Asio/TLS client, channels, move/mute/kick
│   ├── VoiceScripts.cpp           player/map/group/server/world hooks
│   └── shared/                    portable code also used by the client (mini protobuf codec, MVCP, Mumble framing)
├── conf/mod_voicechat.conf.dist
├── data/sql/db-characters/        (only if persistence is needed, e.g. voice mutes)
├── external/
│   ├── client/                    voice.dll, loader patcher, test CLI, 3rd-party
│   └── murmur/                    mumble-server.ini template, ACL/bot setup, docker-compose
└── docs/{de,en}/
```

### 6.2 Phases

| Phase | Content | Result / test |
|---|---|---|
| **3 – PoC** | `voicecore` (C++17, portable): TLS, Mumble handshake, CryptSetup/OCB2, protobuf UDP, ping, UDP tunnel fallback, Opus, miniaudio. **Test CLI** (Linux/Windows) | 2 CLI clients talk via Murmur. Automated test in the container against a real Murmur |
| | voice.dll skeleton + loader: starts with WoW, connects via `voice.ini`, PTT via CVar `PushToTalkButton`, logout disconnects | WoW A ↔ Murmur ↔ WoW B (tested by you) |
| **4 – Positional** | main-thread snapshot, object manager mapping, distance and panning, VoiceTargets (nearby sessions), context | volume and direction change while walking |
| **5 – AC** | module: config, MVCP, bot, nonce binding, channels per map/instance, login/logout/map/instance change, permissions and flags, GM commands (`.voice status/mute/kick`) | no eavesdropping across instance boundaries (test checklist) |
| **6 – Occlusion** | TraceLine integration, ray budget, smoothing, low-pass, configurable factors | a wall audibly dampens |
| **7 – Native UI** | Lua API overrides for the Blizzard voice UI, voice flag in `SMSG_FEATURE_SYSTEM_STATUS`, speaker display, mute list, device selection, game audio ducking | feels like original WoW voice |
| 8 – Extras | party "radio" (second PTT, no positions), yell/whisper (range), HRTF, moderation | – |

### 6.3 What I need from you

- **Client tests** from phase 3b on: I can build here and test against Murmur, but I can't run WoW.
- **Verification of the ❓ addresses** (a checklist comes with phase 3b; optionally x32dbg/IDA).
- **MPQ extracts:** not needed for now, the FrameXML is public.
- ~~Loader decision~~: done, `external/client/loader/`: 29 bytes in an int3 gap, file size and PE header unchanged, compatible with St0ny's patcher.

---

## 7. Open hypotheses (to verify)

| # | Hypothesis | Check |
|---|---|---|
| H1 | The Comsat codec is not Opus-compatible | irrelevant for the design; optional strings analysis of Wow.exe |
| H2 | `EnableVoiceChat=1` without a roster starts no interfering capture | important since phase 7: toggling it in the menu may start the old engine (further callers of 0x9868C0: 0x7DC7F9, 0x7E002D). Test on the client; intercept the engine start if needed |
| H3 | Taking over a voice SMSG handler via `SetMessageHandler` (0x631FA0) is stable | PoC in phase 5 |
| H4 | `TraceLine` at 0x7A3B70 with terrain/WMO/M2 flags | confirmed statically (see 3.7), runtime test pending |
| H5 | Map ID address 0xBD088C vs. 0xAB63BC | in-game comparison (continent vs. instance) |
| H6 | A `LoadFunctions` hook allows overriding the Blizzard voice Lua functions after /reload | not needed: the Lua bridge reinstalls itself idempotently every 5 s (see 3.11) |
| H7 | The Blizzard UI reacts correctly to self-fired `VOICE_*` events | handler signatures checked against FrameXML, Lua test green, runtime test pending |

## 8. Sources

- ReynoldsCahoon/WotLK-Mumble-Positional-Voice (`plugin/wow3.cpp`, `mumo-module/wowrp.py`), at `0960c7e`
- azerothcore/azerothcore-wotlk `3d28fa7`; issue #5063; TrinityCore issue #15057
- SkyFire/ascent_classic `src/ascent-voicechat`, `src/ascent-world/VoiceChatHandler.cpp`, `Group.cpp`
- celguar/voicechat-server (+ wiki with Blizzard documentation from 2008); cmangos/mangos-tbc PR #668
- wowgaming/3.3.5-interface-files (FrameXML 3.3.5)
- mumble-voip/mumble `7bbd2c1` (`Mumble.proto`, `MumbleUDP.proto`, `murmur/Server.cpp`, `Messages.cpp`, `AudioReceiverBuffer.cpp`, `ACL.h`, `mumble-server.ini`)
- Alyst3r/Sylian1337 WotLK-Extensions (MIT), FrostAtom/awesome_wotlk (GPL-3.0, reference only)
- TeamSpeak client SDK docs (3D sound, `onCustom3dRolloffCalculationClientEvent`); miniaudio, Opus, speexdsp

## Appendix: TeamSpeak vs. Mumble (technical only)

| Criterion | TeamSpeak SDK | Mumble/Murmur |
|---|---|---|
| 3D in the client | yes, incl. rolloff callback and sample callbacks (occlusion possible) | no, we render ourselves (needed anyway for WoW occlusion) |
| Protocol implementable by us | no (closed), the vendor DLL must ship in the client | yes, open and documented |
| Embedding in a GPL client | proprietary library inside GPL code, distribution is problematic | BSD, no problem |
| Server | proprietary, controlled via ServerQuery | open source, controlled via protocol bot or Ice |
| 32-bit Windows | current SDK availability ❓ | built ourselves |
| Verdict | no technical advantage, since we do the rendering ourselves anyway | **chosen** |
