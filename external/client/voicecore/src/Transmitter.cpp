/*
 * mod-voicechat - send side (see Transmitter.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "voicecore/Transmitter.h"

#include <algorithm>
#include <cmath>

namespace voicecore
{
    namespace
    {
        constexpr int VAD_HANGOVER_FRAMES = 15;   // 300 ms Nachlauf / hangover
    }

    bool Transmitter::Init(int bitrate)
    {
        _frame.reserve(FRAME_SAMPLES);
        return _enc.Init(bitrate);
    }

    void Transmitter::PushPcm(const float* mono, size_t n)
    {
        float g = _gain;
        for (size_t i = 0; i < n; ++i)
        {
            _frame.push_back(std::clamp(mono[i] * g, -1.0f, 1.0f));
            if (_frame.size() == size_t(FRAME_SAMPLES))
            {
                ProcessFrame();
                _frame.clear();
            }
        }
    }

    void Transmitter::ProcessFrame()
    {
        double sum = 0.0;
        for (float v : _frame) sum += double(v) * v;
        float rms = float(std::sqrt(sum / FRAME_SAMPLES));
        _level = rms;

        bool gate = false;
        switch (_mode.load())
        {
            case TransmitMode::PushToTalk: gate = _ptt; break;
            case TransmitMode::Continuous: gate = true; break;
            case TransmitMode::VoiceActivation:
                if (rms >= _vadThreshold) _hangover = VAD_HANGOVER_FRAMES;
                gate = _hangover > 0;
                if (_hangover > 0) --_hangover;
                break;
        }
        if (_muted) gate = false;

        if (!gate && !_talking) return;

        uint8_t pkt[MAX_OPUS_PACKET];
        int len = _enc.Encode(_frame.data(), pkt, MAX_OPUS_PACKET);
        if (len <= 0) return;

        // DE: Beim Loslassen letztes Paket mit Terminator. EN: on release send last packet with terminator.
        bool terminator = !gate && _talking;
        _talking = gate;
        if (onPacket) onPacket(pkt, size_t(len), terminator);
    }
}
