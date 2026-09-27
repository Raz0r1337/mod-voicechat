/*
 * mod-voicechat - voice.dll main logic (see VoiceApp.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Ablauf: Welt betreten -> Mikrofon/Lautsprecher oeffnen -> mit Murmur
 *     verbinden (Name = Charakter, Kontext = Map). Jedes Audiopaket traegt die
 *     eigene Position; empfangene Stimmen werden nach Entfernung und Richtung
 *     gemischt. Logout -> trennen und Mikrofon freigeben. Verbindungsabbruch
 *     -> Reconnect mit Backoff (2 s .. 30 s).
 * EN: Flow: enter world -> open microphone/speakers -> connect to Murmur
 *     (name = character, context = map). Every audio packet carries the own
 *     position; received voices are mixed by distance and direction.
 *     Logout -> disconnect and release the microphone. Connection lost ->
 *     reconnect with backoff (2 s .. 30 s).
 */
#include "VoiceApp.h"

#include <cmath>

using namespace voicecore;

namespace voice
{
    namespace
    {
        constexpr unsigned long long TICK_STALE_MS = 5000;   // Hauptthread haengt (Ladebildschirm) / main thread stalled
    }

    void VoiceApp::Run()
    {
        std::string ini = Config::ModuleDir() + "\\voice.ini";
        bool haveIni = _cfg.Load(ini);
        SetLogEnabled(_cfg.log);
        Log("mod-voicechat voice.dll started (phase 4), ini " + std::string(haveIni ? "loaded" : "missing -> defaults"));

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
            float pos[3];
            bool has;
            {
                std::lock_guard<std::mutex> g(_posMutex);
                has = _hasMyPos;
                pos[0] = _myPos[0]; pos[1] = _myPos[1]; pos[2] = _myPos[2];
            }
            _client.SendAudio(p, n, term, has ? pos : nullptr);
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
            bool inWorld = _cfg.autoConnectInWorld ? (fresh ? s.inWorld : _active) : true;
            std::string name = !_cfg.username.empty() ? _cfg.username : (fresh ? s.name : _currentName);

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

            if (_active && (!inWorld || name != _currentName))
                Disconnect(inWorld ? "character changed" : "left world");

            if (_active && _lost)
            {
                _client.Stop();
                _active = false;
                _nextRetry = now + _retryDelayMs;
                _retryDelayMs = _retryDelayMs * 2 > 30000 ? 30000 : _retryDelayMs * 2;
            }

            if (!_active && inWorld && !name.empty() && now >= _nextRetry)
                Connect(name);

            if (_active && _client.State() == ClientState::Connected)
            {
                _retryDelayMs = 2000;
                if (fresh && s.inWorld) UpdateContext(s);
                bool fg = _game.Get().tickMs != 0;   // Fenster existiert / window exists
                HWND w = wow::MainWindow();
                bool foreground = fg && w && GetForegroundWindow() == w;
                _tx.SetPushToTalk(foreground && (GetAsyncKeyState(_cfg.pushToTalkKey) & 0x8000) != 0);
            }
            else
                _tx.SetPushToTalk(false);

            Sleep(16);
        }
        Disconnect("shutdown");
    }

    void VoiceApp::UpdateContext(const GameSnapshot& s)
    {
        // DE: Murmur leitet Positionen nur zwischen gleichem Kontext weiter -> Map-Trennung.
        //     (Ab Phase 5 gibt der Server den Kontext inkl. Instanz vor.)
        // EN: Murmur only forwards positions between equal contexts -> map isolation.
        //     (From phase 5 on the server dictates the context incl. instance.)
        std::string ctx = "wow335|" + std::to_string(s.mapId);
        if (ctx == _context) return;
        _context = ctx;
        _client.SetPluginContext(ctx, s.name);
        Log("context " + ctx);
    }

    SpeakerGain VoiceApp::GainFor(uint32_t session)
    {
        std::lock_guard<std::mutex> g(_posMutex);
        auto it = _speakerPos.find(session);
        if (it == _speakerPos.end())
        {
            // DE: Ohne Position (andere Map/kein WoW-Client) standardmaessig stumm.
            // EN: without position (other map/not a WoW client) silent by default.
            SpeakerGain z;
            if (!_cfg.hearWithoutPosition) z.left = z.right = 0.0f;
            return z;
        }
        return spatial::Compute(_listener, it->second, _spatial);
    }

    void VoiceApp::Connect(const std::string& name)
    {
        if (!_audio.Running() && !_audio.Start(_cfg.inputDevice, _cfg.outputDevice))
            Log("audio start failed");
        ClientConfig cc;
        cc.host = _cfg.host;
        cc.port = _cfg.port;
        cc.username = name;
        cc.password = _cfg.password;
        cc.certPinSha256 = _cfg.certSha256;
        cc.forceTcp = _cfg.forceTcp;
        _lost = false;
        _context.clear();
        _client.Start(cc);
        _active = true;
        _currentName = name;
        Log("connecting as '" + name + "'");
    }

    void VoiceApp::Disconnect(const char* why)
    {
        if (!_active) return;
        Log(std::string("disconnect: ") + why);
        _tx.SetPushToTalk(false);
        _client.Stop();
        _audio.Stop();            // Mikrofon freigeben / release microphone
        _mixer.Clear();
        {
            std::lock_guard<std::mutex> g(_posMutex);
            _speakerPos.clear();
        }
        _active = false;
        _lost = false;
        _currentName.clear();
        _context.clear();
        _retryDelayMs = 2000;
        _nextRetry = 0;
    }
}
