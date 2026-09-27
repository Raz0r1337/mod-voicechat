/*
 * mod-voicechat - spatial audio math (see Spatial.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "voicecore/Spatial.h"

#include <algorithm>
#include <cmath>

namespace voicecore::spatial
{
    namespace
    {
        constexpr float PI = 3.14159265358979323846f;
    }

    Vec3 WowToMumble(const Vec3& w)
    {
        return { -w.y * YARDS_TO_METERS, w.z * YARDS_TO_METERS, w.x * YARDS_TO_METERS };
    }

    Vec3 MumbleToWow(const Vec3& m)
    {
        return { m.z / YARDS_TO_METERS, -m.x / YARDS_TO_METERS, m.y / YARDS_TO_METERS };
    }

    float DistanceGain(float d, const Params& p)
    {
        if (d <= p.minDistance) return 1.0f;
        if (d >= p.maxDistance || p.maxDistance <= p.minDistance) return 0.0f;
        // DE: weicher Abfall: (1 - t)^2. EN: smooth falloff: (1 - t)^2.
        float t = (d - p.minDistance) / (p.maxDistance - p.minDistance);
        return (1.0f - t) * (1.0f - t);
    }

    SpeakerGain Compute(const Listener& l, const Vec3& s, const Params& p, float occlusion,
                        float occlusionGain, float occlusionLowpassHz)
    {
        float dx = s.x - l.pos.x, dy = s.y - l.pos.y, dz = s.z - l.pos.z;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        float level = DistanceGain(dist, p);

        SpeakerGain g;
        g.left = g.right = 0.0f;
        if (level <= 0.0f) return g;

        // DE: Richtung in der Ebene relativ zur Blickrichtung. EN: direction in the plane relative to facing.
        float pan = 0.0f, front = 1.0f;
        float planar = std::sqrt(dx * dx + dy * dy);
        if (planar > 0.05f)
        {
            float fx = std::cos(l.yaw), fy = std::sin(l.yaw);     // vorne / forward
            float rx = fy, ry = -fx;                              // rechts / right (WoW: Y = Westen / west)
            pan = std::clamp((dx * rx + dy * ry) / planar, -1.0f, 1.0f);
            front = (dx * fx + dy * fy) / planar;                 // 1 vorne, -1 hinten / 1 front, -1 behind
        }

        // DE: Hinten leicht leiser und dumpfer (Vorne/Hinten-Hinweis). EN: behind slightly quieter and duller.
        float rear = std::clamp(-front, 0.0f, 1.0f);
        level *= 1.0f - rear * (1.0f - p.rearAttenuation);
        float lowpass = rear > 0.0f ? p.rearLowpassHz + (1.0f - rear) * (20000.0f - p.rearLowpassHz) : 0.0f;

        // DE: Verdeckung (Phase 6). EN: occlusion (phase 6).
        occlusion = std::clamp(occlusion, 0.0f, 1.0f);
        if (occlusion > 0.0f)
        {
            level *= 1.0f - occlusion * (1.0f - occlusionGain);
            if (occlusionLowpassHz > 0.0f)
            {
                float olp = 20000.0f + occlusion * (occlusionLowpassHz - 20000.0f);
                lowpass = lowpass > 0.0f ? std::min(lowpass, olp) : olp;
            }
        }

        // DE: Equal-Power-Panning. EN: equal-power panning.
        float angle = (pan + 1.0f) * PI / 4.0f;
        g.left = std::cos(angle) * level * 1.41421356f;
        g.right = std::sin(angle) * level * 1.41421356f;
        g.left = std::min(g.left, 1.0f);
        g.right = std::min(g.right, 1.0f);
        g.lowpassHz = lowpass;
        return g;
    }
}
