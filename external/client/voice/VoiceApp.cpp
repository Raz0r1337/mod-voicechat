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

#include <algorithm>
#include <cmath>

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
        Log("mod-voicechat voice.dll started (phase 5), ini " + std::string(haveIni ? "loaded" : "missing -> defaults") +
            ", mode " + _cfg.serverMode);

        _spatial.minDistance = _cfg.minDistance;
        _spatial.maxDistance = _cfg.maxDistance;

        if (!_tx.Init(_cfg.bitrate)) { Log("opus init failed - voice disabled"); return; }
        _tx.SetMode(_cfg.mode == "vad" ? TransmitMode::VoiceActivation
                  : _cfg.mode == "continuous" ? TransmitMode::Continuous
                  : TransmitMode::PushToTalk);
        _tx.SetVadThreshold(_cfg.vadThreshold);
        _tx.SetInputGain(_cfg.inputGain);
        _mixer.SetMasterVolume(_cfg.outputVolume);

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
        _audio.onCapture = [this](const float* mono, size_t frames) { _tx.PushPcm(mono, frames); };
        _audio.onPlayback = [this](float* stereo, size_t frames) {
            _mixer.Mix(stereo, frames, [this](uint32_t s) { return GainFor(s); });
        };
        _client.onAudio = [this](const MumbleProto::UdpAudio& a) {
            {
                std::lock_guard<std::mutex> g(_posMutex);
                if (a.hasPosition) _speakerPos[a.senderSession] = spatial::MumbleToWow({ *a.pos[0], *a.pos[1], *a.pos[2] });
                else _speakerPos.erase(a.senderSession);
            }
            _mixer.Push(a.senderSession, a.frameNumber, a.opus, a.isTerminator);
        };
        _client.onLog = [](const std::string& m) { Log("mumble: " + m); };
        _client.onDisconnected = [this](const std::string& r) { Log("mumble disconnected: " + r); _lost = true; };
        // DE: MVCP-Pakete im Hauptthread senden/empfangen. EN: send/receive MVCP packets on the main thread.
        _game.onTick = [this](const GameSnapshot& s) { _server.MainTick(s.inWorld); };

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

            if (_active && _client.State() == ClientState::Connected)
            {
                HWND w = wow::MainWindow();
                bool foreground = _game.Get().tickMs != 0 && w && GetForegroundWindow() == w;
                _tx.SetPushToTalk(foreground && (GetAsyncKeyState(_cfg.pushToTalkKey) & 0x8000) != 0);
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
        _nextHello = now + (_helloTries < AUTO_HELLO_TRIES ? HELLO_INTERVAL_MS : 30000);
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
                    std::lock_guard<std::mutex> g(_posMutex);
                    _spatial.minDistance = c.minDistance;
                    _spatial.maxDistance = c.maxDistance;
                }
                _link = Link::Configured;
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
                // DE: "unavailable" = Murmur weg -> spaeter erneut; sonst erst nach neuem Betreten der Welt.
                // EN: "unavailable" = Murmur gone -> retry later; otherwise only after re-entering the world.
                _nextHello = d.reason == "unavailable" ? now + 30000 : 0;
                break;
            }
            default:
                break;
        }
    }

    void VoiceApp::StepServer(unsigned long long now)
    {
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
        return spatial::Compute(_listener, it->second, _spatial);
    }

    void VoiceApp::Connect(const ClientConfig& cc)
    {
        if (!_audio.Running() && !_audio.Start(_cfg.inputDevice, _cfg.outputDevice))
            Log("audio start failed");
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
            _nearby.clear();
            _group.clear();
            _serverLists = false;
            _spatial.minDistance = _cfg.minDistance;
            _spatial.maxDistance = _cfg.maxDistance;
        }
        _target.clear();
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
