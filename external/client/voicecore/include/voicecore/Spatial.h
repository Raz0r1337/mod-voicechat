/*
 * mod-voicechat - spatial audio math (pure, testable, no WoW dependencies)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: WoW-Koordinaten (Yards): X = Norden, Y = Westen, Z = oben. Blickrichtung
 *     0 = Norden, gegen den Uhrzeigersinn (Richtung Westen) steigend.
 *     Im Mumble-Paket (Meter, linkshaendig, Y oben) wie wow3.dll:
 *     mumble = (-wowY, wowZ, wowX) * 0.9144
 * EN: WoW coordinates (yards): X = north, Y = west, Z = up. Facing 0 = north,
 *     increasing counter-clockwise (towards west).
 *     In the Mumble packet (metres, left-handed, Y up) as in wow3.dll:
 *     mumble = (-wowY, wowZ, wowX) * 0.9144
 */
#pragma once

#include "AudioMixer.h"

namespace voicecore::spatial
{
    struct Vec3 { float x = 0, y = 0, z = 0; };

    struct Listener
    {
        Vec3 pos;          // WoW-Yards / WoW yards
        float yaw = 0;     // Radiant, WoW-Konvention / radians, WoW convention
    };

    struct Params
    {
        float minDistance = 3.0f;      // bis hier volle Lautstaerke / full volume up to here
        float maxDistance = 40.0f;     // ab hier stumm / silent from here on
        float rearAttenuation = 0.85f; // Pegel direkt hinter dem Hoerer / level right behind the listener
        float rearLowpassHz = 6000.0f; // leichte Dumpfheit hinten / slight muffling behind
    };

    constexpr float YARDS_TO_METERS = 0.9144f;

    Vec3 WowToMumble(const Vec3& wow);
    Vec3 MumbleToWow(const Vec3& mumble);

    // DE: Lautstaerke 0..1 nur aus der Entfernung. EN: volume 0..1 from distance only.
    float DistanceGain(float distance, const Params& p);

    // DE: Links/rechts, Tiefpass und Pegel fuer einen Sprecher.
    //     occlusion 0 = frei, 1 = vollstaendig verdeckt (Phase 6).
    // EN: left/right, low-pass and level for one speaker.
    //     occlusion 0 = clear, 1 = fully occluded (phase 6).
    SpeakerGain Compute(const Listener& l, const Vec3& speaker, const Params& p, float occlusion = 0.0f,
                        float occlusionGain = 1.0f, float occlusionLowpassHz = 0.0f);
}
