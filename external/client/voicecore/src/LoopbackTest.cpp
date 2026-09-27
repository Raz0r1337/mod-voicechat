/*
 * mod-voicechat - microphone test for the voice menu (see LoopbackTest.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "voicecore/LoopbackTest.h"

#include <algorithm>
#include <cmath>

namespace voicecore
{
    float LoopbackTest::ToLevel(float rms)
    {
        // DE: -60 dBFS .. 0 dBFS -> 0 .. 100. EN: -60 dBFS .. 0 dBFS -> 0 .. 100.
        if (rms <= 1e-6f) return 0.0f;
        float db = 20.0f * std::log10(rms);
        return std::clamp((db + 60.0f) / 60.0f * 100.0f, 0.0f, 100.0f);
    }

    void LoopbackTest::StartRecording(double seconds)
    {
        std::lock_guard<std::mutex> g(_mutex);
        seconds = std::clamp(seconds, 0.5, 30.0);
        _buf.clear();
        _limit = size_t(seconds * SAMPLE_RATE);
        _buf.reserve(_limit);
        _recording = true;
        _playing = false;
        _level = 0.0f;
    }

    void LoopbackTest::StopRecording()
    {
        std::lock_guard<std::mutex> g(_mutex);
        _recording = false;
    }

    void LoopbackTest::StartPlayback()
    {
        std::lock_guard<std::mutex> g(_mutex);
        _recording = false;
        _pos = 0;
        _playing = !_buf.empty();
    }

    void LoopbackTest::StopPlayback()
    {
        std::lock_guard<std::mutex> g(_mutex);
        _playing = false;
    }

    bool LoopbackTest::IsRecording() const { std::lock_guard<std::mutex> g(_mutex); return _recording; }
    bool LoopbackTest::IsPlaying() const { std::lock_guard<std::mutex> g(_mutex); return _playing; }
    int LoopbackTest::Level() const { std::lock_guard<std::mutex> g(_mutex); return int(_level + 0.5f); }
    void LoopbackTest::SetInputGain(float g) { std::lock_guard<std::mutex> l(_mutex); _gain = g; }
    void LoopbackTest::SetOutputVolume(float v) { std::lock_guard<std::mutex> l(_mutex); _volume = v; }

    void LoopbackTest::OnCapture(const float* mono, size_t n)
    {
        std::lock_guard<std::mutex> g(_mutex);
        if (!_recording || n == 0) return;
        double sum = 0.0;
        for (size_t i = 0; i < n && _buf.size() < _limit; ++i)
        {
            float v = std::clamp(mono[i] * _gain, -1.0f, 1.0f);
            _buf.push_back(v);
            sum += double(v) * v;
        }
        _level = ToLevel(float(std::sqrt(sum / double(n))));
        if (_buf.size() >= _limit) _recording = false;   // DE: Zeit abgelaufen / time is up
    }

    void LoopbackTest::MixPlayback(float* stereo, size_t frames)
    {
        std::lock_guard<std::mutex> g(_mutex);
        if (!_playing || frames == 0) return;
        double sum = 0.0;
        size_t i = 0;
        for (; i < frames && _pos < _buf.size(); ++i, ++_pos)
        {
            float v = _buf[_pos] * _volume;
            stereo[2 * i] = std::clamp(stereo[2 * i] + v, -1.0f, 1.0f);
            stereo[2 * i + 1] = std::clamp(stereo[2 * i + 1] + v, -1.0f, 1.0f);
            sum += double(_buf[_pos]) * _buf[_pos];
        }
        _level = i ? ToLevel(float(std::sqrt(sum / double(i)))) : 0.0f;
        if (_pos >= _buf.size()) { _playing = false; _level = 0.0f; }
    }
}
