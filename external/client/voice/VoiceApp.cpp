/*
 * mod-voicechat - voice.dll main logic (see VoiceApp.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Ablauf: Welt betreten -> Mikrofon/Lautsprecher oeffnen -> mit Murmur
 *     verbinden (Name = Charakter). Logout -> trennen und Mikrofon freigeben.
 *     Verbindungsabbruch -> Reconnect mit Backoff (2 s .. 30 s).
 * EN: Flow: enter world -> open microphone/speakers -> connect to Murmur
 *     (name = character). Logout -> disconnect and release the microphone.
 *     Connection lost -> reconnect with backoff (2 s .. 30 s).
 */
#include "VoiceApp.h"

#include <windows.h>

namespace voice
{
    void VoiceApp::Run()
    {
        std::string ini = Config::ModuleDir() + "\\voice.ini";
        bool haveIni = _cfg.Load(ini);
        SetLogEnabled(_cfg.log);
        Log("mod-voicechat voice.dll started (phase 3), ini " + std::string(haveIni ? "loaded" : "missing -> defaults"));
        _wow.Configure(_cfg.inWorldAddress, _cfg.nameAddress);

        if (!_tx.Init(_cfg.bitrate)) { Log("opus init failed - voice disabled"); return; }
        _tx.SetMode(_cfg.mode == "vad" ? voicecore::TransmitMode::VoiceActivation
                  : _cfg.mode == "continuous" ? voicecore::TransmitMode::Continuous
                  : voicecore::TransmitMode::PushToTalk);
        _tx.SetVadThreshold(_cfg.vadThreshold);
        _tx.SetInputGain(_cfg.inputGain);
        _mixer.SetMasterVolume(_cfg.outputVolume);

        // DE: Verkabelung Audio <-> Netz. EN: wiring audio <-> network.
        _tx.onPacket = [this](const uint8_t* p, size_t n, bool term) { _client.SendAudio(p, n, term, nullptr); };
        _audio.onCapture = [this](const float* mono, size_t frames) { _tx.PushPcm(mono, frames); };
        _audio.onPlayback = [this](float* stereo, size_t frames) { _mixer.Mix(stereo, frames, nullptr); };
        _client.onAudio = [this](const MumbleProto::UdpAudio& a) { _mixer.Push(a.senderSession, a.frameNumber, a.opus, a.isTerminator); };
        _client.onLog = [](const std::string& m) { Log("mumble: " + m); };
        _client.onDisconnected = [this](const std::string& r) { Log("mumble disconnected: " + r); _lost = true; };

        while (!_stop)
        {
            bool inWorld = _cfg.autoConnectInWorld ? _wow.InWorld() : true;
            std::string name = !_cfg.username.empty() ? _cfg.username : (inWorld ? _wow.CharacterName() : std::string());
            unsigned long long now = GetTickCount64();

            if (_active && (!inWorld || name != _currentName))
                Disconnect(inWorld ? "character changed" : "left world");

            if (_active && _lost)
            {
                // DE: Verbindung weg -> spaeter neu verbinden. EN: connection gone -> reconnect later.
                _client.Stop();
                _active = false;
                _nextRetry = now + _retryDelayMs;
                _retryDelayMs = _retryDelayMs * 2 > 30000 ? 30000 : _retryDelayMs * 2;
            }

            if (!_active && inWorld && !name.empty() && now >= _nextRetry)
                Connect(name);

            if (_active && _client.State() == voicecore::ClientState::Connected)
            {
                _retryDelayMs = 2000;
                bool ptt = WowBridge::IsForeground() && (GetAsyncKeyState(_cfg.pushToTalkKey) & 0x8000) != 0;
                _tx.SetPushToTalk(ptt);
            }
            else
                _tx.SetPushToTalk(false);

            Sleep(10);
        }
        Disconnect("shutdown");
    }

    void VoiceApp::Connect(const std::string& name)
    {
        if (!_audio.Running() && !_audio.Start(_cfg.inputDevice, _cfg.outputDevice))
            Log("audio start failed");
        voicecore::ClientConfig cc;
        cc.host = _cfg.host;
        cc.port = _cfg.port;
        cc.username = name;
        cc.password = _cfg.password;
        cc.certPinSha256 = _cfg.certSha256;
        cc.forceTcp = _cfg.forceTcp;
        _lost = false;
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
        _active = false;
        _lost = false;
        _currentName.clear();
        _retryDelayMs = 2000;
        _nextRetry = 0;
    }
}
