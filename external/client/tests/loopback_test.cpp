/*
 * mod-voicechat - unit tests for voicecore::LoopbackTest (microphone test)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "voicecore/LoopbackTest.h"

#include <cmath>
#include <cstdio>
#include <vector>

using voicecore::LoopbackTest;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

int main()
{
    std::vector<float> tone(480), silence(480, 0.0f);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = 0.5f * float(std::sin(2.0 * 3.14159265 * 440.0 * double(i) / 48000.0));

    LoopbackTest t;
    CHECK(!t.Active() && t.Level() == 0);
    t.StartRecording(1.0);                                   // 1 s = 100 Bloecke / blocks
    CHECK(t.IsRecording());
    t.OnCapture(tone.data(), tone.size());
    int loud = t.Level();
    CHECK(loud > 80 && loud <= 100);                         // -9 dBFS -> ~85
    t.OnCapture(silence.data(), silence.size());
    CHECK(t.Level() == 0);
    for (int i = 0; i < 200; ++i) t.OnCapture(tone.data(), tone.size());
    CHECK(!t.IsRecording());                                 // DE: nach 1 s automatisch aus / stops after 1 s

    std::vector<float> out(480 * 2, 0.0f);
    t.StartPlayback();
    CHECK(t.IsPlaying() && !t.IsRecording());
    t.MixPlayback(out.data(), 480);                          // 1. Block = Ton / 1st block = tone
    CHECK(std::fabs(out[2] - tone[1]) < 1e-6f && out[2] == out[3]);
    CHECK(t.Level() > 80);
    int blocks = 1;
    while (t.IsPlaying() && blocks < 1000) { std::fill(out.begin(), out.end(), 0.0f); t.MixPlayback(out.data(), 480); ++blocks; }
    CHECK(blocks == 100 && t.Level() == 0);                  // genau 1 s / exactly 1 s

    // DE: Gain/Lautstaerke und Stopp. EN: gain/volume and stop.
    t.SetInputGain(0.5f);
    t.SetOutputVolume(0.5f);
    t.StartRecording(0.5);
    t.OnCapture(tone.data(), tone.size());
    t.StopRecording();
    CHECK(!t.IsRecording());
    std::fill(out.begin(), out.end(), 0.0f);
    t.StartPlayback();
    t.MixPlayback(out.data(), 480);
    CHECK(std::fabs(out[2] - tone[1] * 0.25f) < 1e-6f);
    t.StopPlayback();
    CHECK(!t.Active());

    if (g_fail == 0) std::printf("loopback_test: all checks passed\n");
    return g_fail == 0 ? 0 : 1;
}
