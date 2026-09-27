/*
 * mod-voicechat - unit tests for spatial math and mixer ramps (no framework, exit code = failures)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "voicecore/AudioMixer.h"
#include "voicecore/OpusCodec.h"
#include "voicecore/Spatial.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace voicecore;
using namespace voicecore::spatial;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

int main()
{
    const float PI = 3.14159265f;
    Params p;                       // min 3 yd, max 40 yd
    Listener l;                     // Ursprung, Blick nach Norden (+X) / origin, facing north (+X)

    // --- Entfernung / distance ---
    CHECK(DistanceGain(1.0f, p) == 1.0f);
    CHECK(DistanceGain(3.0f, p) == 1.0f);
    CHECK(DistanceGain(40.0f, p) == 0.0f);
    CHECK(DistanceGain(100.0f, p) == 0.0f);
    CHECK(DistanceGain(10.0f, p) > DistanceGain(20.0f, p));
    CHECK(DistanceGain(20.0f, p) > DistanceGain(30.0f, p));

    // --- vorne: beide Seiten gleich / front: both sides equal ---
    SpeakerGain front = Compute(l, { 5, 0, 0 }, p);
    CHECK(std::fabs(front.left - front.right) < 1e-3f);
    CHECK(front.lowpassHz == 0.0f);

    // --- WoW: +Y = Westen = links bei Blick nach Norden / +Y = west = left when facing north ---
    SpeakerGain left = Compute(l, { 0, 5, 0 }, p);
    CHECK(left.left > 0.9f && left.right < 0.1f);
    SpeakerGain right = Compute(l, { 0, -5, 0 }, p);
    CHECK(right.right > 0.9f && right.left < 0.1f);

    // --- Drehung: Blick nach Westen (yaw = +90 Grad) -> Westen ist vorne / facing west -> west is front ---
    Listener lw = l; lw.yaw = PI / 2;
    SpeakerGain westFront = Compute(lw, { 0, 5, 0 }, p);
    CHECK(std::fabs(westFront.left - westFront.right) < 1e-3f);
    SpeakerGain northRight = Compute(lw, { 5, 0, 0 }, p);   // Norden ist jetzt rechts / north is now right
    CHECK(northRight.right > northRight.left);

    // --- hinten: leiser + Tiefpass / behind: quieter + low-pass ---
    SpeakerGain behind = Compute(l, { -5, 0, 0 }, p);
    CHECK(behind.left < front.left);
    CHECK(behind.lowpassHz > 0.0f && behind.lowpassHz < 20000.0f);

    // --- ausserhalb der Reichweite stumm / out of range silent ---
    SpeakerGain far = Compute(l, { 100, 0, 0 }, p);
    CHECK(far.left == 0.0f && far.right == 0.0f);

    // --- Hoehe zaehlt zur Entfernung / height counts towards distance ---
    CHECK(Compute(l, { 0, 0, 50 }, p).left == 0.0f);

    // --- Occlusion: leiser + Tiefpass / quieter + low-pass ---
    SpeakerGain occ = Compute(l, { 5, 0, 0 }, p, 1.0f, 0.25f, 800.0f);
    CHECK(std::fabs(occ.left - front.left * 0.25f) < 1e-3f);
    CHECK(std::fabs(occ.lowpassHz - 800.0f) < 1.0f);

    // --- Gruppe: nur Richtung, nie leiser / group: direction only, never quieter ---
    {
        Params g = p;
        g.directionOnly = true;
        SpeakerGain gf = Compute(l, { 500, 0, 0 }, g);                    // weit vorne / far in front
        CHECK(std::fabs(gf.left - 1.0f) < 1e-3f && std::fabs(gf.right - 1.0f) < 1e-3f);
        SpeakerGain gb = Compute(l, { -500, 0, 0 }, g, 1.0f, 0.25f, 800.0f); // hinten + verdeckt / behind + occluded
        CHECK(std::fabs(gb.left - 1.0f) < 1e-3f && gb.lowpassHz == 0.0f);
        SpeakerGain gl = Compute(l, { 0, 500, 0 }, g);                    // links / left
        CHECK(gl.left > 0.99f && gl.right < 0.01f);
    }

    // --- Koordinaten hin und zurueck / coordinates round trip ---
    Vec3 w{ 1234.5f, -567.25f, 89.75f };
    Vec3 back = MumbleToWow(WowToMumble(w));
    CHECK(std::fabs(back.x - w.x) < 1e-2f && std::fabs(back.y - w.y) < 1e-2f && std::fabs(back.z - w.z) < 1e-2f);

    // --- Mixer: Rampe ohne Spruenge bei Pegelwechsel / mixer: ramp without jumps on level change ---
    {
        OpusEnc enc; enc.Init(32000);
        AudioMixer mixer;
        std::vector<float> tone(FRAME_SAMPLES);
        uint8_t pkt[MAX_OPUS_PACKET];
        for (int f = 0; f < 10; ++f)
        {
            for (int i = 0; i < FRAME_SAMPLES; ++i) tone[size_t(i)] = 0.5f;   // DC -> leicht pruefbar / easy to check
            int n = enc.Encode(tone.data(), pkt, MAX_OPUS_PACKET);
            mixer.Push(1, uint64_t(f) * 2, std::vector<uint8_t>(pkt, pkt + n), false);
        }
        std::vector<float> out(FRAME_SAMPLES * 2);
        float gain = 1.0f;
        auto fn = [&](uint32_t) { SpeakerGain g; g.left = g.right = gain; return g; };
        mixer.Mix(out.data(), FRAME_SAMPLES, fn);
        mixer.Mix(out.data(), FRAME_SAMPLES, fn);
        gain = 0.0f;                                        // harter Sprung / hard jump
        mixer.Mix(out.data(), FRAME_SAMPLES, fn);
        float maxStep = 0.0f;
        for (size_t i = 1; i < size_t(FRAME_SAMPLES); ++i)
            maxStep = std::max(maxStep, std::fabs(out[2 * i] - out[2 * (i - 1)]));
        CHECK(maxStep < 0.05f);                             // keine Stufe / no step
        CHECK(std::fabs(out[2 * (FRAME_SAMPLES - 1)]) < 0.01f);   // am Ende leise / quiet at the end
    }

    std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "all spatial tests passed", g_fail);
    return g_fail;
}
