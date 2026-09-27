/*
 * mod-voicechat - receive side mixer (see AudioMixer.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "voicecore/AudioMixer.h"
#include "voicecore/OpusCodec.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace voicecore
{
    namespace
    {
        constexpr size_t JITTER_START_PACKETS = 2;   // ~40 ms Vorpuffer / pre-buffer
        constexpr size_t MAX_QUEUE_PACKETS    = 12;  // danach aufholen / catch up beyond this
        constexpr int    MAX_PLC_FRAMES       = 4;   // ~80 ms Verlust kaschieren / conceal
        constexpr int    SAMPLES_PER_FRAMENO  = 480; // Mumble zaehlt 10-ms-Frames / counts 10 ms frames
    }

    struct AudioMixer::Speaker
    {
        OpusDec dec;
        std::map<uint64_t, std::pair<std::vector<uint8_t>, bool>> queue;   // frameNumber -> (opus, terminator)
        std::deque<float> pcm;
        uint64_t nextFrame = 0;
        bool started = false;
        bool ending = false;
        int plc = 0;
        bool talking = false;
        bool hasGain = false;
        float lastLeft = 0.0f, lastRight = 0.0f;
        float lpState = 0.0f;
    };

    AudioMixer::AudioMixer() = default;
    AudioMixer::~AudioMixer() = default;

    void AudioMixer::Push(uint32_t session, uint64_t frameNumber, const std::vector<uint8_t>& opus, bool terminator)
    {
        std::lock_guard<std::mutex> g(_mutex);
        auto& sp = _speakers[session];
        if (!sp)
        {
            sp = std::make_unique<Speaker>();
            if (!sp->dec.Init()) { _speakers.erase(session); return; }
        }
        if (sp->started && frameNumber < sp->nextFrame)
            return;   // zu spaet / too late
        sp->queue[frameNumber] = { opus, terminator };
        if (terminator) sp->ending = true;
        while (sp->queue.size() > MAX_QUEUE_PACKETS)
        {
            // DE: Latenz begrenzen: aeltestes verwerfen. EN: limit latency: drop the oldest.
            sp->queue.erase(sp->queue.begin());
            if (sp->started) sp->nextFrame = sp->queue.begin()->first;
        }
    }

    void AudioMixer::Mix(float* out, size_t frames, const GainFn& gainFn)
    {
        std::fill(out, out + frames * 2, 0.0f);
        std::lock_guard<std::mutex> g(_mutex);
        float tmp[5760];   // max. 120 ms Opus-Paket / max 120 ms opus packet

        for (auto& kv : _speakers)
        {
            Speaker& s = *kv.second;
            if (!s.started)
            {
                if (s.queue.size() >= JITTER_START_PACKETS || (s.ending && !s.queue.empty()))
                {
                    s.started = true;
                    s.nextFrame = s.queue.begin()->first;
                    s.plc = 0;
                }
                else
                    continue;
            }

            // DE: PCM-Puffer auffuellen. EN: refill the PCM buffer.
            while (s.pcm.size() < frames && s.started)
            {
                auto it = s.queue.find(s.nextFrame);
                if (it == s.queue.end() && !s.queue.empty() && s.queue.begin()->first < s.nextFrame)
                {
                    s.queue.erase(s.queue.begin());
                    continue;
                }
                int n;
                if (it != s.queue.end())
                {
                    n = s.dec.Decode(it->second.first.data(), it->second.first.size(), tmp, 5760);
                    bool term = it->second.second;
                    s.queue.erase(it);
                    s.plc = 0;
                    if (term) s.ending = true;
                }
                else if (s.ending && s.queue.empty())
                {
                    s.started = false;   // Ende der Uebertragung / end of transmission
                    s.ending = false;
                    break;
                }
                else if (++s.plc > MAX_PLC_FRAMES)
                {
                    s.started = false;
                    s.ending = false;
                    s.queue.clear();
                    break;
                }
                else
                    n = s.dec.Decode(nullptr, 0, tmp, FRAME_SAMPLES);

                if (n <= 0) { n = FRAME_SAMPLES; std::fill(tmp, tmp + n, 0.0f); }
                s.pcm.insert(s.pcm.end(), tmp, tmp + n);
                s.nextFrame += uint64_t(std::max(1, n / SAMPLES_PER_FRAMENO));
            }

            SpeakerGain gain = gainFn ? gainFn(kv.first) : SpeakerGain();
            if (!s.hasGain) { s.lastLeft = gain.left; s.lastRight = gain.right; s.hasGain = true; }
            // DE: Einpoliger Tiefpass (a = 1 -> aus). EN: one-pole low-pass (a = 1 -> off).
            float a = gain.lowpassHz > 0.0f
                ? 1.0f - std::exp(-2.0f * 3.14159265f * std::min(gain.lowpassHz, 20000.0f) / float(SAMPLE_RATE))
                : 1.0f;
            size_t take = std::min(frames, s.pcm.size());
            for (size_t i = 0; i < take; ++i)
            {
                float v = s.pcm[i] * _master;
                s.lpState += a * (v - s.lpState);
                v = s.lpState;
                // DE: Pegel ueber den Block rampen (kein Knacken bei Bewegung). EN: ramp level across the block (no clicks when moving).
                float t = float(i + 1) / float(take);
                out[2 * i] += v * (s.lastLeft + (gain.left - s.lastLeft) * t);
                out[2 * i + 1] += v * (s.lastRight + (gain.right - s.lastRight) * t);
            }
            if (take > 0) { s.lastLeft = gain.left; s.lastRight = gain.right; }
            s.pcm.erase(s.pcm.begin(), s.pcm.begin() + ptrdiff_t(take));
            s.talking = s.started || take > 0;
        }

        for (size_t i = 0; i < frames * 2; ++i)
            out[i] = std::clamp(out[i], -1.0f, 1.0f);
    }

    std::vector<uint32_t> AudioMixer::Talking() const
    {
        std::lock_guard<std::mutex> g(_mutex);
        std::vector<uint32_t> r;
        for (const auto& kv : _speakers)
            if (kv.second->talking) r.push_back(kv.first);
        return r;
    }

    void AudioMixer::Remove(uint32_t session)
    {
        std::lock_guard<std::mutex> g(_mutex);
        _speakers.erase(session);
    }

    void AudioMixer::Clear()
    {
        std::lock_guard<std::mutex> g(_mutex);
        _speakers.clear();
    }
}
