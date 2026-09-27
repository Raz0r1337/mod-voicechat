/*
 * mod-voicechat - microphone test for the voice menu (pure, testable)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Nimmt bis zu N Sekunden vom Mikrofon auf und spielt sie danach ab - wie der
 *     Mikrofontest im Blizzard-Voice-Menue. Level() liefert 0..100 fuer den VU-Meter
 *     (waehrend der Aufnahme das Mikrofon, waehrend der Wiedergabe das Abgespielte).
 *     Thread-sicher: Aufnahme-/Wiedergabe-Threads und WoW-Hauptthread (Lua).
 * EN: records up to N seconds from the microphone and plays them back afterwards - like
 *     the microphone test in the Blizzard voice menu. Level() returns 0..100 for the VU
 *     meter (microphone while recording, played audio while playing).
 *     Thread-safe: capture/playback threads and the WoW main thread (Lua).
 */
#pragma once

#include <cstddef>
#include <mutex>
#include <vector>

namespace voicecore
{
    class LoopbackTest
    {
    public:
        static constexpr int SAMPLE_RATE = 48000;

        void StartRecording(double seconds);
        void StopRecording();
        void StartPlayback();
        void StopPlayback();
        bool IsRecording() const;
        bool IsPlaying() const;
        bool Active() const { return IsRecording() || IsPlaying(); }
        int Level() const;             // 0..100

        void SetInputGain(float g);
        void SetOutputVolume(float v);

        // DE: Audio-Threads. EN: audio threads.
        void OnCapture(const float* mono, size_t n);
        void MixPlayback(float* stereo, size_t frames);

    private:
        static float ToLevel(float rms);

        mutable std::mutex _mutex;
        std::vector<float> _buf;
        size_t _limit = 0, _pos = 0;
        bool _recording = false, _playing = false;
        float _gain = 1.0f, _volume = 1.0f;
        float _level = 0.0f;
    };
}
