/*
 * mod-voicechat - voice.dll main logic (see VoiceApp.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Ablauf (Mode=auto/server):
 *     Welt betreten -> HELLO an den Worldserver -> CONFIG (Murmur-Adresse, Name, Nonce)
 *     -> mit Murmur verbinden -> "MVC1-BIND <nonce>" an den Bot -> BOUND. Danach:
 *     CONTEXT (Map/Instanz) und NEARBY (nahe Fremde + Gruppe) vom Server.
 *     Abspielen: Gruppe/Raid immer voll (Position nur fuer die Richtung), Fremde nach
 *     Entfernung, alle anderen stumm. Senden: Whisper an Nah + Gruppe.
 *     Antwortet der Server nicht (Mode=auto), laeuft alles eigenstaendig wie in Phase 4.
 * EN: Flow (Mode=auto/server):
 *     enter world -> HELLO to the worldserver -> CONFIG (Murmur address, name, nonce)
 *     -> connect to Murmur -> "MVC1-BIND <nonce>" to the bot -> BOUND. Then:
 *     CONTEXT (map/instance) and NEARBY (nearby strangers + group) from the server.
 *     Playback: party/raid always full (position only for direction), strangers by
 *     distance, everyone else silent. Sending: whisper to near + group.
 *     If the server does not answer (Mode=auto), everything runs standalone as in phase 4.
 */
#include "VoiceApp.h"
#include "LuaLoopback.h"

#include <algorithm>
#include <cmath>
#include <iterator>

using namespace voicecore;

namespace voice
{
    namespace
    {
        constexpr unsigned long long TICK_STALE_MS = 5000;   // Hauptthread haengt (Ladebildschirm) / main thread stalled
        constexpr int AUTO_HELLO_TRIES = 3;                  // danach Fallback / then fallback (Mode=auto)
        constexpr unsigned HELLO_INTERVAL_MS = 5000;
        constexpr unsigned BIND_INTERVAL_MS = 3000;
        constexpr int BIND_TRIES = 5;
    }

    void VoiceApp::Run()
    {
        std::string ini = Config::ModuleDir() + "\\voice.ini";
        bool haveIni = _cfg.Load(ini);
        SetLogEnabled(_cfg.log);
        Log("mod-voicechat voice.dll started (phase 6), ini " + std::string(haveIni ? "loaded" : "missing -> defaults") +
            ", mode " + _cfg.serverMode);

        if (_cfg.serverMode != "auto" && _cfg.serverMode != "server" && _cfg.serverMode != "standalone")
        {
            Log("unknown [Server] Mode '" + _cfg.serverMode + "' -> auto");
            _cfg.serverMode = "auto";
        }
        _spatial.minDistance = _cfg.minDistance;
        _spatial.maxDistance = _cfg.maxDistance;
        _cfg.occlusionRaysPerTick = std::clamp(_cfg.occlusionRaysPerTick, OcclusionTracker::RAYS_PER_SPEAKER, 60);
        _cfg.occlusionGain = std::clamp(_cfg.occlusionGain, 0.0f, 1.0f);
        _occOff = !_cfg.occlusion;
        {
            OcclusionParams op;
            op.headHeight = _cfg.occlusionHeadHeight;
            _occ.SetParams(op);
        }

        if (!_tx.Init(_cfg.bitrate)) { Log("opus init failed - voice disabled"); return; }
        _tx.SetMode(_cfg.mode == "vad" ? TransmitMode::VoiceActivation
                  : _cfg.mode == "continuous" ? TransmitMode::Continuous
                  : TransmitMode::PushToTalk);
        _tx.SetVadThreshold(_cfg.vadThreshold);
        _tx.SetInputGain(_cfg.inputGain);
        _mixer.SetMasterVolume(_cfg.outputVolume);
        _pttKeys = { _cfg.pushToTalkKey };

        // DE: Verkabelung Audio <-> Netz. EN: wiring audio <-> network.
        _tx.onPacket = [this](const uint8_t* p, size_t n, bool term) {
            int target = _sendTarget;
            if (target < 0) return;
            float pos[3];
            bool has;
            {
                std::lock_guard<std::mutex> g(_posMutex);
                has = _hasMyPos;
                pos[0] = _myPos[0]; pos[1] = _myPos[1]; pos[2] = _myPos[2];
            }
            _client.SendAudio(p, n, term, has ? pos : nullptr, uint32_t(target));
        };
        _audio.onCapture = [this](const float* mono, size_t frames) {
            _tx.PushPcm(mono, frames);
            _loop.OnCapture(mono, frames);
        };
        _audio.onPlayback = [this](float* stereo, size_t frames) {
            _mixer.Mix(stereo, frames, [this](uint32_t s) { return GainFor(s); });
            _loop.MixPlayback(stereo, frames);
        };
        BindLoopback(&_loop);
        _client.onAudio = [this](const MumbleProto::UdpAudio& a) {
            {
                std::lock_guard<std::mutex> g(_posMutex);
                if (a.hasPosition) _speakerPos[a.senderSession] = spatial::MumbleToWow({ *a.pos[0], *a.pos[1], *a.pos[2] });
                else _speakerPos.erase(a.senderSession);
                _speakerSeen[a.senderSession] = GetTickCount64();
            }
            _mixer.Push(a.senderSession, a.frameNumber, a.opus, a.isTerminator);
        };
        _client.onLog = [](const std::string& m) { Log("mumble: " + m); };
        _client.onDisconnected = [this](const std::string& r) { Log("mumble disconnected: " + r); _lost = true; };
        // DE: MVCP-Pakete im Hauptthread senden/empfangen. EN: send/receive MVCP packets on the main thread.
        _game.onTick = [this](const GameSnapshot& s) {
            _server.MainTick(s.inWorld);
            _ui.MainTick(s.inWorld);
            _duck.MainTick(s.inWorld);
            if (s.inWorld && !_occOff) TraceOcclusion(s);
        };

        unsigned long long lastAttachTry = 0;
        while (!_stop)
        {
            unsigned long long now = GetTickCount64();
            if (now - lastAttachTry > 1000) { lastAttachTry = now; _game.EnsureAttached(); }
            _game.RequestTick();

            GameSnapshot s = _game.Get();
            bool fresh = s.tickMs != 0 && now - s.tickMs < TICK_STALE_MS;
            // DE: Waehrend Ladebildschirmen (kein Tick) den Zustand beibehalten.
            // EN: keep the state during loading screens (no tick).
            bool inWorld = fresh ? s.inWorld : _inWorld;
            if (!_cfg.autoConnectInWorld && _cfg.serverMode == "standalone") inWorld = true;
            std::string charName = fresh && s.inWorld ? s.name : _charName;

            if (fresh && s.inWorld)
            {
                // DE: Hoerer = Charakter; Blickrichtung Kamera -> Charakter (3rd Person) oder Charakter-Facing.
                // EN: listener = character; orientation camera -> character (3rd person) or character facing.
                spatial::Listener l;
                l.pos = { s.pos.x, s.pos.y, s.pos.z };
                l.yaw = s.facing;
                if (_cfg.listenerMode == "camera" && s.cameraValid)
                {
                    float dx = s.pos.x - s.camera.x, dy = s.pos.y - s.camera.y;
                    if (dx * dx + dy * dy > 0.25f) l.yaw = std::atan2(dy, dx);
                }
                spatial::Vec3 m = spatial::WowToMumble(l.pos);
                std::lock_guard<std::mutex> g(_posMutex);
                _listener = l;
                _myPos[0] = m.x; _myPos[1] = m.y; _myPos[2] = m.z;
                _hasMyPos = true;
            }

            // DE: Welt betreten/verlassen oder Charakterwechsel. EN: world enter/leave or character change.
            if (inWorld != _inWorld || (inWorld && !charName.empty() && charName != _charName))
            {
                Disconnect(inWorld ? "character changed" : "left world");
                _inWorld = inWorld;
                _charName = charName;
                ResetLink(now);
            }

            std::vector<uint8_t> m;
            while (_server.Poll(m))
                HandleMvcp(m, now);

            if (_inWorld)
            {
                switch (_link)
                {
                    case Link::Hello:      StepHello(now); break;
                    case Link::Configured: StepServer(now); break;
                    case Link::Standalone: StepStandalone(now, s, fresh); break;
                    case Link::Disabled:
                        if (_nextHello && now >= _nextHello) { _link = Link::Hello; _helloTries = 0; }
                        break;
                    case Link::Off: break;
                }
            }

            _ui.SetActive(_nativeUi && _inWorld);
            ApplyAudioSettings();
            UpdateDevices(now);
            if (now >= _nextUiState)
            {
                _nextUiState = now + 100;
                UpdateUiState();
            }

            if (!_occOff && now >= _nextOccUpdate)
            {
                _nextOccUpdate = now + 100;
                UpdateOcclusionCandidates(now);
            }

            if (_active && _client.State() == ClientState::Connected)
            {
                HWND w = wow::MainWindow();
                bool foreground = _game.Get().tickMs != 0 && w && GetForegroundWindow() == w;
                _tx.SetPushToTalk(foreground && PushToTalkDown());
            }
            else
                _tx.SetPushToTalk(false);

            Sleep(16);
        }
        Disconnect("shutdown");
    }

    // ------------------------------------------------------------------------------------------
    //  Server-Link (MVCP)
    // ------------------------------------------------------------------------------------------
    void VoiceApp::ResetLink(unsigned long long now)
    {
        _server.ClearOutgoing();
        _helloTries = 0;
        _helloBackoffMs = 2000;
        _nextHello = now + 1000;   // DE: kurz nach dem Betreten. EN: shortly after entering.
        _nativeUi = false;
        _userOff = false;
        if (!_inWorld) _link = Link::Off;
        else _link = _cfg.serverMode == "standalone" ? Link::Standalone : Link::Hello;
    }

    void VoiceApp::StepHello(unsigned long long now)
    {
        if (now < _nextHello) return;
        if (_cfg.serverMode == "auto" && _helloTries >= AUTO_HELLO_TRIES)
        {
            Log("no answer from mod-voicechat on the worldserver -> standalone (voice.ini [Server])");
            _link = Link::Standalone;
            return;
        }
        VoiceProto::Hello h;
        _server.Send(VoiceProto::Encode(h));
        ++_helloTries;
        // DE: auto: nach AUTO_HELLO_TRIES * 5 s Fallback. server: danach nur noch alle 30 s.
        // EN: auto: fallback after AUTO_HELLO_TRIES * 5 s. server: afterwards only every 30 s.
        _nextHello = now + (_cfg.serverMode == "auto" || _helloTries < AUTO_HELLO_TRIES ? HELLO_INTERVAL_MS : 30000);
    }

    void VoiceApp::HandleMvcp(const std::vector<uint8_t>& m, unsigned long long now)
    {
        VoiceProto::Msg type;
        const uint8_t* body = nullptr;
        size_t n = 0;
        if (!VoiceProto::Unwrap(m.data(), m.size(), type, body, n) || !_inWorld)
            return;

        switch (type)
        {
            case VoiceProto::Msg::Config:
            {
                VoiceProto::Config c;
                if (!VoiceProto::Decode(body, n, c) || c.host.empty() || c.nonce.empty()) return;
                if (_link == Link::Standalone)
                    Disconnect("server integration available");
                bool same = _active && _link == Link::Configured && c.host == _srv.host && c.port == _srv.port &&
                            c.username == _srv.username;
                // DE: Schon verbunden (z. B. Bot-Reconnect) -> nur neu binden. EN: already connected (e.g. bot reconnect) -> just re-bind.
                if (!same)
                {
                    Disconnect("new server config");
                    _nextRetry = 0;
                }
                _srv = c;
                _srvContext = c.context;
                {
                    // DE: Plausibel halten. EN: keep sane.
                    float minD = c.minDistance >= 0.0f && c.minDistance < 1000.0f ? c.minDistance : _cfg.minDistance;
                    float maxD = c.maxDistance > minD && c.maxDistance < 5000.0f ? c.maxDistance : minD + 37.0f;
                    std::lock_guard<std::mutex> g(_posMutex);
                    _spatial.minDistance = minD;
                    _spatial.maxDistance = maxD;
                }
                _link = Link::Configured;
                _nativeUi = c.nativeUi && _cfg.nativeUi;
                _bound = false;
                _bindTries = 0;
                _nextBind = now;
                Log("server config: " + c.host + ":" + std::to_string(c.port) + " as '" + c.username + "'");
                break;
            }
            case VoiceProto::Msg::Context:
            {
                VoiceProto::Context c;
                if (VoiceProto::Decode(body, n, c)) _srvContext = c.context;
                break;
            }
            case VoiceProto::Msg::Nearby:
            {
                VoiceProto::Nearby l;
                if (!VoiceProto::Decode(body, n, l)) return;
                std::set<uint32_t> nearby(l.sessions.begin(), l.sessions.end()), group(l.group.begin(), l.group.end());
                _target.clear();
                std::set_union(nearby.begin(), nearby.end(), group.begin(), group.end(), std::back_inserter(_target));
                std::lock_guard<std::mutex> g(_posMutex);
                _nearby.swap(nearby);
                _group.swap(group);
                _serverLists = true;
                break;
            }
            case VoiceProto::Msg::Bound:
                _bound = true;
                _retryDelayMs = 2000;
                _helloBackoffMs = 2000;
                Log("bound to the character");
                break;
            case VoiceProto::Msg::Disabled:
            {
                VoiceProto::Disabled d;
                VoiceProto::Decode(body, n, d);
                Log("voice disabled by the server: " + d.reason);
                Disconnect("disabled by the server");
                _link = Link::Disabled;
                _nativeUi = false;
                // DE: "unavailable" (Murmur weg) -> in 30 s erneut, "disabled" (Modul aus) -> in 5 min;
                //     Rechte/Kick -> erst nach neuem Betreten der Welt.
                // EN: "unavailable" (Murmur gone) -> retry in 30 s, "disabled" (module off) -> in 5 min;
                //     permissions/kick -> only after re-entering the world.
                _nextHello = d.reason == "unavailable" ? now + 30000 : d.reason == "disabled" ? now + 300000 : 0;
                break;
            }
            default:
                break;
        }
    }

    void VoiceApp::StepServer(unsigned long long now)
    {
        if (_nativeUi)
        {
            // DE: Blizzard-UI: "Voice-Chat aktivieren" im WoW-Menue ist der Hauptschalter.
            // EN: Blizzard UI: "Enable voice chat" in the WoW menu is the master switch.
            WowVoiceSettings ws = _ui.Settings();
            if (!ws.valid) return;   // DE: CVars noch nicht gelesen / CVars not read yet
            if (!ws.enabled)
            {
                if (_active) Disconnect("voice chat switched off in the WoW options");
                if (!_userOff) Log("voice chat is off in the WoW options (Interface -> Sound & Voice -> Voice)");
                _userOff = true;
                return;
            }
            if (_userOff)
            {
                // DE: Wieder an -> frische CONFIG (die Nonce ist evtl. abgelaufen). EN: on again -> fresh CONFIG (nonce may have expired).
                _userOff = false;
                _link = Link::Hello;
                _helloTries = 0;
                _nextHello = now;
                return;
            }
        }

        if (_active && _lost)
        {
            // DE: Neue Verbindung braucht eine neue Nonce -> wieder HELLO (mit Backoff).
            // EN: a new connection needs a new nonce -> HELLO again (with backoff).
            Disconnect("connection lost");
            _link = Link::Hello;
            _helloTries = 0;
            _nextHello = now + _helloBackoffMs;
            _helloBackoffMs = std::min(_helloBackoffMs * 2, 30000u);
            return;
        }
        if (!_active)
        {
            if (now < _nextRetry) return;
            ClientConfig cc;
            cc.host = _srv.host;
            cc.port = uint16_t(_srv.port);
            cc.username = _srv.username;
            cc.password = _srv.password;
            cc.certPinSha256 = !_srv.certPin.empty() ? _srv.certPin : _cfg.certSha256;
            cc.forceTcp = _cfg.forceTcp;
            Connect(cc);
            return;
        }
        if (_client.State() != ClientState::Connected) return;

        if (_srvContext != _context)
        {
            _context = _srvContext;
            _client.SetPluginContext(_context, _charName);
            Log("context " + _context);
        }

        if (!_bound && now >= _nextBind)
        {
            if (_bindTries >= BIND_TRIES)
            {
                Log("binding failed -> asking the server again");
                Disconnect("binding failed");
                _link = Link::Hello;
                _helloTries = 0;
                _nextHello = now + _helloBackoffMs;
                _helloBackoffMs = std::min(_helloBackoffMs * 2, 30000u);
                return;
            }
            uint32_t bot = 0;
            for (auto& u : _client.Users())
                if (u.name == _srv.botName) bot = u.session;
            if (bot)
            {
                MumbleProto::TextMessage t;
                t.sessions.push_back(bot);
                t.message = VoiceProto::BindText(_srv.nonce);
                _client.SendTcp(MumbleProto::Tcp::TextMessage, t.Encode());
            }
            ++_bindTries;
            _nextBind = now + BIND_INTERVAL_MS;
        }

        // DE: Whisper-Ziel = nahe Fremde + Gruppe. EN: whisper target = nearby strangers + group.
        if (_bound && (!_targetValid || _target != _targetSent))
        {
            _client.SendTcp(MumbleProto::Tcp::VoiceTarget, MumbleProto::EncodeVoiceTargetSessions(1, _target));
            _targetSent = _target;
            _targetValid = true;
        }
        _sendTarget = _bound && _targetValid && !_target.empty() ? 1 : -1;
    }

    void VoiceApp::StepStandalone(unsigned long long now, const GameSnapshot& s, bool fresh)
    {
        std::string name = !_cfg.username.empty() ? _cfg.username : _charName;
        if (_active && _lost)
        {
            _client.Stop();
            _active = false;
            _nextRetry = now + _retryDelayMs;
            _retryDelayMs = std::min(_retryDelayMs * 2, 30000u);
        }
        if (!_active && !name.empty() && now >= _nextRetry)
        {
            ClientConfig cc;
            cc.host = _cfg.host;
            cc.port = _cfg.port;
            cc.username = name;
            cc.password = _cfg.password;
            cc.certPinSha256 = _cfg.certSha256;
            cc.forceTcp = _cfg.forceTcp;
            Connect(cc);
        }
        if (_active && _client.State() == ClientState::Connected)
        {
            _retryDelayMs = 2000;
            _sendTarget = 0;
            if (fresh && s.inWorld) UpdateContext(s);
        }
    }

    void VoiceApp::UpdateContext(const GameSnapshot& s)
    {
        // DE: Murmur leitet Positionen nur zwischen gleichem Kontext weiter -> Map-Trennung.
        // EN: Murmur only forwards positions between equal contexts -> map isolation.
        std::string ctx = "wow335|" + std::to_string(s.mapId);
        if (ctx == _context) return;
        _context = ctx;
        _client.SetPluginContext(ctx, s.name);
        Log("context " + ctx);
    }

    // ------------------------------------------------------------------------------------------
    //  Blizzard-UI (Phase 7)
    // ------------------------------------------------------------------------------------------
    void VoiceApp::ApplyAudioSettings()
    {
        WowVoiceSettings ws = _nativeUi ? _ui.Settings() : WowVoiceSettings();
        if (!ws.valid)
        {
            // DE: voice.ini. EN: voice.ini.
            _tx.SetMode(_cfg.mode == "vad" ? TransmitMode::VoiceActivation
                      : _cfg.mode == "continuous" ? TransmitMode::Continuous
                      : TransmitMode::PushToTalk);
            _tx.SetMuted(false);
            _tx.SetVadThreshold(_cfg.vadThreshold);
            _tx.SetInputGain(_cfg.inputGain);
            _mixer.SetMasterVolume(_cfg.outputVolume);
            _loop.SetInputGain(_cfg.inputGain);
            _loop.SetOutputVolume(_cfg.outputVolume);
            if (!_pttBinding.empty()) { _pttBinding.clear(); _pttKeys = { _cfg.pushToTalkKey }; }
            return;
        }
        _tx.SetMode(ws.voiceActivation ? TransmitMode::VoiceActivation : TransmitMode::PushToTalk);
        _tx.SetMuted(!ws.microphone);
        // DE: Empfindlichkeit 1 = reagiert auf leise Stimmen. EN: sensitivity 1 = reacts to quiet voices.
        _tx.SetVadThreshold(0.005f + (1.0f - ws.vadSensitivity) * 0.05f);
        _tx.SetInputGain(ws.inputGain);
        _mixer.SetMasterVolume(ws.outputVolume);
        _loop.SetInputGain(ws.inputGain);
        _loop.SetOutputVolume(ws.outputVolume);
        std::string binding = ws.pushToTalk.empty() ? std::string("-") : ws.pushToTalk;
        if (binding != _pttBinding)
        {
            _pttBinding = binding;
            _pttKeys = ws.pushToTalk.empty() ? std::vector<int>() : ParseWowBinding(ws.pushToTalk);
            if (_pttKeys.empty())
            {
                _pttKeys = { _cfg.pushToTalkKey };
                Log("push-to-talk: WoW binding '" + ws.pushToTalk + "' not usable -> voice.ini key");
            }
            else
                Log("push-to-talk: WoW binding '" + ws.pushToTalk + "'");
        }
    }

    void VoiceApp::DesiredDevices(std::string& in, std::string& out)
    {
        in = _cfg.inputDevice;
        out = _cfg.outputDevice;
        if (!_nativeUi) return;
        WowVoiceSettings ws = _ui.Settings();
        if (!ws.valid) return;
        // DE: Index 0 = Standard (bzw. voice.ini), 1..n = Liste im Voice-Menue. EN: index 0 = default (or voice.ini), 1..n = menu list.
        if (ws.inputDevice > 0 && ws.inputDevice <= int(_capNames.size())) in = _capNames[size_t(ws.inputDevice - 1)];
        if (ws.outputDevice > 0 && ws.outputDevice <= int(_playNames.size())) out = _playNames[size_t(ws.outputDevice - 1)];
    }

    void VoiceApp::UpdateDevices(unsigned long long now)
    {
        // DE: Mikrofontest braucht Audio auch ohne Verbindung. EN: the microphone test needs audio even without a connection.
        if (_loop.Active() && !_audio.Running())
        {
            DesiredDevices(_curIn, _curOut);
            _audioForTest = _audio.Start(_curIn, _curOut);
        }
        else if (_audioForTest && !_loop.Active() && !_active)
        {
            _audio.Stop();
            _audioForTest = false;
        }
        if (_nativeUi && now >= _nextDevEnum)
        {
            // DE: Liste fuers Menue auffrischen (Headset ein-/ausgesteckt). EN: refresh the menu list (headset plugged in/out).
            _nextDevEnum = now + 30000;
            if (AudioIO::ListDevices(_capNames, _playNames))
                _ui.SetDevices(_capNames, _playNames);
        }
        if (!_audio.Running()) return;
        std::string in, out;
        DesiredDevices(in, out);
        if (in == _curIn && out == _curOut) return;
        Log("audio devices changed in the voice menu -> reopening");
        _audio.Stop();
        _curIn = in;
        _curOut = out;
        if (!_audio.Start(_curIn, _curOut)) Log("audio start failed");
    }

    bool VoiceApp::PushToTalkDown() const
    {
        if (_pttKeys.empty()) return false;
        for (int vk : _pttKeys)
            if (!(GetAsyncKeyState(vk) & 0x8000)) return false;
        return true;
    }

    void VoiceApp::UpdateUiState()
    {
        // DE: Wer hat Voice, wer spricht (nur ich + Gruppe; Fremde haben keinen Rahmen).
        // EN: who has voice, who is talking (only me + group; strangers have no frame).
        std::set<std::string> talking, voiceNames, plates;
        bool othersTalking = false;
        if (_nativeUi && _active && _bound && _client.State() == ClientState::Connected && !_charName.empty())
        {
            voiceNames.insert(_charName);
            if (_tx.IsTalking()) talking.insert(_charName);
            std::map<uint32_t, std::string> names;
            for (const auto& u : _client.Users())
                names[u.session] = u.name.substr(0, u.name.find('@'));
            std::set<uint32_t> group;
            {
                std::lock_guard<std::mutex> g(_posMutex);
                group = _group;
            }
            for (uint32_t s : group)
            {
                auto it = names.find(s);
                if (it != names.end()) voiceNames.insert(it->second);
            }
            std::set<uint32_t> nearby;
            {
                std::lock_guard<std::mutex> g(_posMutex);
                nearby = _nearby;
            }
            for (uint32_t s : _mixer.Talking())
            {
                auto it = names.find(s);
                if (group.count(s) && it != names.end()) talking.insert(it->second);
                if (group.count(s) || nearby.count(s))
                {
                    othersTalking = true;   // hoerbar / audible
                    if (it != names.end() && s != _client.Session()) plates.insert(it->second);
                }
            }
        }
        _ui.SetState(talking, voiceNames, plates);

        // DE: Spielgeraeusche absenken, solange jemand anderes hoerbar spricht (Regler im Voice-Menue).
        // EN: lower game sounds while someone else is audibly talking (sliders in the voice menu).
        WowVoiceSettings ws = _nativeUi ? _ui.Settings() : WowVoiceSettings();
        _duck.SetTarget(_cfg.duckGameSound && ws.valid && othersTalking, ws.duckSound, ws.duckMusic, ws.duckAmbience);
    }

    // ------------------------------------------------------------------------------------------
    //  Occlusion (Phase 6)
    // ------------------------------------------------------------------------------------------
    void VoiceApp::UpdateOcclusionCandidates(unsigned long long now)
    {
        // DE: Nur Fremde, die gerade sprechen und in Reichweite sind; Gruppe nie (nur Richtung).
        // EN: only strangers who are talking and in range; never the group (direction only).
        std::vector<std::pair<uint32_t, spatial::Vec3>> list;
        {
            std::lock_guard<std::mutex> g(_posMutex);
            for (auto it = _speakerSeen.begin(); it != _speakerSeen.end();)
            {
                if (now - it->second > 60000) it = _speakerSeen.erase(it);   // alte Eintraege / old entries
                else ++it;
            }
            float range = _spatial.maxDistance;
            for (const auto& kv : _speakerPos)
            {
                auto seen = _speakerSeen.find(kv.first);
                if (seen == _speakerSeen.end() || now - seen->second > 1500) continue;
                if (_serverLists && (_group.count(kv.first) || !_nearby.count(kv.first))) continue;
                float dx = kv.second.x - _listener.pos.x, dy = kv.second.y - _listener.pos.y, dz = kv.second.z - _listener.pos.z;
                if (dx * dx + dy * dy + dz * dz > range * range) continue;
                list.emplace_back(kv.first, kv.second);
            }
        }
        _occ.SetCandidates(list);
    }

    namespace
    {
        struct RayCtx { wow::C3Vector from, to; uint32_t flags; bool hit; };
        void TraceRay(void* p)
        {
            auto* c = static_cast<RayCtx*>(p);
            c->hit = wow::TraceLine(c->from, c->to, c->flags);
        }
    }

    void VoiceApp::TraceOcclusion(const GameSnapshot& s)
    {
        uint32_t flags = _cfg.occlusionFlags;
        _occ.Trace({ s.pos.x, s.pos.y, s.pos.z }, _cfg.occlusionRaysPerTick, GetTickCount64(),
            [this, flags](const spatial::Vec3& from, const spatial::Vec3& to) {
                if (_occOff) return false;
                RayCtx c{ { from.x, from.y, from.z }, { to.x, to.y, to.z }, flags, false };
                if (!wow::Guarded(&TraceRay, &c))
                {
                    _occOff = true;
                    Log("occlusion: access violation in TraceLine - occlusion disabled");
                    return false;
                }
                return c.hit;
            });
    }

    // ------------------------------------------------------------------------------------------
    //  Audio
    // ------------------------------------------------------------------------------------------
    SpeakerGain VoiceApp::GainFor(uint32_t session)
    {
        std::lock_guard<std::mutex> g(_posMutex);
        SpeakerGain silent;
        silent.left = silent.right = 0.0f;
        auto it = _speakerPos.find(session);

        if (_serverLists)
        {
            // DE: Gruppe/Raid: nie leiser, Position nur fuer die Richtung (andere Map -> mittig).
            // EN: party/raid: never quieter, position only for direction (other map -> centred).
            if (_group.count(session))
            {
                if (it == _speakerPos.end()) return SpeakerGain();
                spatial::Params p = _spatial;
                p.directionOnly = true;
                return spatial::Compute(_listener, it->second, p);
            }
            // DE: Nur vom Server gemeldete Fremde sind hoerbar. EN: only strangers reported by the server are audible.
            if (!_nearby.count(session)) return silent;
        }
        if (it == _speakerPos.end())
        {
            // DE: Ohne Position (andere Map/kein WoW-Client) standardmaessig stumm.
            // EN: without position (other map/not a WoW client) silent by default.
            return _cfg.hearWithoutPosition ? SpeakerGain() : silent;
        }
        float occ = _occOff ? 0.0f : _occ.Get(session, GetTickCount64());
        return spatial::Compute(_listener, it->second, _spatial, occ, _cfg.occlusionGain, _cfg.occlusionLowpassHz);
    }

    void VoiceApp::Connect(const ClientConfig& cc)
    {
        if (!_audio.Running())
        {
            DesiredDevices(_curIn, _curOut);
            if (!_audio.Start(_curIn, _curOut)) Log("audio start failed");
        }
        _lost = false;
        _context.clear();
        _targetValid = false;
        _sendTarget = -1;
        _client.Start(cc);
        _active = true;
        Log("connecting to " + cc.host + ":" + std::to_string(cc.port) + " as '" + cc.username + "'");
    }

    void VoiceApp::Disconnect(const char* why)
    {
        _sendTarget = -1;
        _bound = false;
        _targetValid = false;
        {
            std::lock_guard<std::mutex> g(_posMutex);
            _speakerPos.clear();
            _speakerSeen.clear();
            _nearby.clear();
            _group.clear();
            _serverLists = false;
            _spatial.minDistance = _cfg.minDistance;
            _spatial.maxDistance = _cfg.maxDistance;
        }
        _target.clear();
        _occ.Clear();
        if (!_active) return;
        Log(std::string("disconnect: ") + why);
        _tx.SetPushToTalk(false);
        _client.Stop();
        _audio.Stop();            // Mikrofon freigeben / release microphone
        _mixer.Clear();
        _active = false;
        _lost = false;
        _context.clear();
        _retryDelayMs = 2000;
        _nextRetry = 0;
    }
}
