/*
 * mod-voicechat - send side: 20 ms framing, push-to-talk / voice activation, Opus encoding
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "OpusCodec.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace voicecore
{
    enum class TransmitMode { PushToTalk, VoiceActivation, Continuous };

    class Transmitter
    {
    public:
        // DE: Wird im Aufnahme-Thread aufgerufen. EN: called on the capture thread.
        std::function<void(const uint8_t* opus, size_t len, bool terminator)> onPacket;

        bool Init(int bitrate = 32000);
        void SetMode(TransmitMode m) { _mode = m; }
        void SetPushToTalk(bool down) { _ptt = down; }
        void SetVadThreshold(float rms) { _vadThreshold = rms; }   // 0..1
        void SetInputGain(float g) { _gain = g; }
        void SetMuted(bool m) { _muted = m; }
        bool IsTalking() const { return _talking; }
        float Level() const { return _level; }

        // DE: 48 kHz mono, beliebige Blockgroesse. EN: 48 kHz mono, any block size.
        void PushPcm(const float* mono, size_t n);

    private:
        void ProcessFrame();

        OpusEnc _enc;
        std::vector<float> _frame;
        std::atomic<TransmitMode> _mode{ TransmitMode::PushToTalk };
        std::atomic<bool> _ptt{ false };
        std::atomic<bool> _muted{ false };
        std::atomic<bool> _talking{ false };
        std::atomic<float> _vadThreshold{ 0.02f };
        std::atomic<float> _gain{ 1.0f };
        std::atomic<float> _level{ 0.0f };
        int _hangover = 0;
    };
}
