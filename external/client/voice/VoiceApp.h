/*
 * mod-voicechat - voice.dll main logic (phase 3: plain voice, no positions yet)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "AudioIO.h"
#include "Config.h"
#include "WowBridge.h"

#include "voicecore/AudioMixer.h"
#include "voicecore/MumbleClient.h"
#include "voicecore/Transmitter.h"

#include <atomic>

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

        Config _cfg;
        WowBridge _wow;
        AudioIO _audio;
        voicecore::MumbleClient _client;
        voicecore::AudioMixer _mixer;
        voicecore::Transmitter _tx;

        std::atomic<bool> _stop{ false };
        std::atomic<bool> _lost{ false };      // Verbindung verloren / connection lost
        bool _active = false;
        std::string _currentName;
        unsigned _retryDelayMs = 2000;
        unsigned long long _nextRetry = 0;
    };
}
