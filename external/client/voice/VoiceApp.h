/*
 * mod-voicechat - voice.dll main logic (phase 4: positional voice)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "AudioIO.h"
#include "Config.h"
#include "GameThread.h"

#include "voicecore/AudioMixer.h"
#include "voicecore/MumbleClient.h"
#include "voicecore/Spatial.h"
#include "voicecore/Transmitter.h"

#include <atomic>
#include <map>
#include <mutex>

namespace voice
{
    class VoiceApp
    {
    public:
        void Run();                       // eigener Thread / own thread
        void RequestStop() { _stop = true; }

    private:
        void Connect(const std::string& name);
        void Disconnect(const char* why);
        void UpdateContext(const GameSnapshot& s);
        voicecore::SpeakerGain GainFor(uint32_t session);

        Config _cfg;
        GameThread _game;
        AudioIO _audio;
        voicecore::MumbleClient _client;
        voicecore::AudioMixer _mixer;
        voicecore::Transmitter _tx;
        voicecore::spatial::Params _spatial;

        // DE: Position der Sprecher (aus den Audiopaketen, WoW-Koordinaten).
        // EN: speaker positions (from the audio packets, WoW coordinates).
        std::mutex _posMutex;
        std::map<uint32_t, voicecore::spatial::Vec3> _speakerPos;
        voicecore::spatial::Listener _listener;
        float _myPos[3] = {};
        bool _hasMyPos = false;

        std::atomic<bool> _stop{ false };
        std::atomic<bool> _lost{ false };
        bool _active = false;
        std::string _currentName;
        std::string _context;
        unsigned _retryDelayMs = 2000;
        unsigned long long _nextRetry = 0;
    };
}
