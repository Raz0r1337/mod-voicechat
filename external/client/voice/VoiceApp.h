/*
 * mod-voicechat - voice.dll main logic (phase 5: server integration)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "AudioIO.h"
#include "Config.h"
#include "GameThread.h"
#include "NativeUi.h"
#include "ServerLink.h"

#include "voicecore/AudioMixer.h"
#include "voicecore/MumbleClient.h"
#include "voicecore/Occlusion.h"
#include "voicecore/Spatial.h"
#include "voicecore/Transmitter.h"

#include "VoiceProtocol.h"

#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace voice
{
    class VoiceApp
    {
    public:
        void Run();                       // eigener Thread / own thread
        void RequestStop() { _stop = true; }

    private:
        // DE: Zustand der Server-Verbindung (MVCP). EN: state of the server link (MVCP).
        enum class Link { Off, Hello, Configured, Disabled, Standalone };

        void ResetLink(unsigned long long now);
        void HandleMvcp(const std::vector<uint8_t>& m, unsigned long long now);
        void StepHello(unsigned long long now);
        void StepServer(unsigned long long now);
        void StepStandalone(unsigned long long now, const GameSnapshot& s, bool fresh);
        void Connect(const voicecore::ClientConfig& cc);
        void Disconnect(const char* why);
        void UpdateContext(const GameSnapshot& s);
        void UpdateOcclusionCandidates(unsigned long long now);
        void ApplyAudioSettings();
        void UpdateUiState();
        bool PushToTalkDown() const;
        void TraceOcclusion(const GameSnapshot& s);   // Hauptthread / main thread
        voicecore::SpeakerGain GainFor(uint32_t session);

        Config _cfg;
        GameThread _game;
        ServerLink _server;
        NativeUi _ui;
        bool _nativeUi = false;          // DE: Server erlaubt + voice.ini will / server allows + voice.ini wants
        bool _userOff = false;           // DE: im WoW-Menue ausgeschaltet / switched off in the WoW menu
        std::string _pttBinding;
        std::vector<int> _pttKeys;
        unsigned long long _nextUiState = 0;
        AudioIO _audio;
        voicecore::MumbleClient _client;
        voicecore::AudioMixer _mixer;
        voicecore::Transmitter _tx;
        voicecore::OcclusionTracker _occ;
        std::atomic<bool> _occOff{ false };     // DE: aus oder TraceLine defekt / off or TraceLine broken
        unsigned long long _nextOccUpdate = 0;

        // DE: Von Audio-Thread und Worker genutzt (_posMutex). EN: used by audio thread and worker (_posMutex).
        std::mutex _posMutex;
        voicecore::spatial::Params _spatial;
        std::map<uint32_t, voicecore::spatial::Vec3> _speakerPos;   // WoW-Koordinaten / WoW coordinates
        std::map<uint32_t, unsigned long long> _speakerSeen;         // letztes Audiopaket / last audio packet
        voicecore::spatial::Listener _listener;
        float _myPos[3] = {};
        bool _hasMyPos = false;
        bool _serverLists = false;              // Server bestimmt, wer hoerbar ist / server decides who is audible
        std::set<uint32_t> _nearby, _group;

        // DE: -1 = nicht senden, 0 = normal (Channel), 1 = Whisper-Ziel 1. EN: -1 = don't send, 0 = normal, 1 = whisper target 1.
        std::atomic<int> _sendTarget{ -1 };
        std::atomic<bool> _stop{ false };
        std::atomic<bool> _lost{ false };

        bool _active = false;
        bool _inWorld = false;
        std::string _charName;
        std::string _context;                   // gesetzt / applied
        unsigned _retryDelayMs = 2000;
        unsigned long long _nextRetry = 0;

        Link _link = Link::Off;
        VoiceProto::Config _srv;
        std::string _srvContext;
        unsigned long long _nextHello = 0;
        int _helloTries = 0;
        unsigned _helloBackoffMs = 2000;
        bool _bound = false;
        int _bindTries = 0;
        unsigned long long _nextBind = 0;
        std::vector<uint32_t> _target, _targetSent;
        bool _targetValid = false;
    };
}
