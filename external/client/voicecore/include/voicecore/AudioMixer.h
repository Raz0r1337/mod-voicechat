/*
 * mod-voicechat - receive side: per-speaker jitter buffer, Opus decoding, stereo mix
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Push() aus dem Netzwerk-Thread, Mix() aus dem Audio-Thread.
 *     Lautstaerke/Panning pro Sprecher kommt ueber gainFn (Phase 4: 3D).
 * EN: Push() from the network thread, Mix() from the audio thread.
 *     Per-speaker volume/panning comes via gainFn (phase 4: 3D).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace voicecore
{
    struct SpeakerGain
    {
        float left = 1.0f;
        float right = 1.0f;
        float lowpassHz = 0.0f;   // 0 = aus / off
    };

    class AudioMixer
    {
    public:
        using GainFn = std::function<SpeakerGain(uint32_t session)>;

        AudioMixer();
        ~AudioMixer();

        void Push(uint32_t session, uint64_t frameNumber, const std::vector<uint8_t>& opus, bool terminator);
        // DE: stereoOut = frames * 2 Floats (interleaved). EN: stereoOut = frames * 2 floats (interleaved).
        void Mix(float* stereoOut, size_t frames, const GainFn& gainFn);
        std::vector<uint32_t> Talking() const;
        void Remove(uint32_t session);
        void Clear();
        void SetMasterVolume(float v) { _master = v; }

    private:
        struct Speaker;
        mutable std::mutex _mutex;
        std::map<uint32_t, std::unique_ptr<Speaker>> _speakers;
        float _master = 1.0f;
    };
}
