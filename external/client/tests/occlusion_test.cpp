/*
 * mod-voicechat - unit tests for the occlusion tracker (no WoW needed)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Ersetzt WoWs TraceLine durch eine Test-Wand und prueft Strahlanteil,
 *     Budget, Glaettung und Aufraeumen.
 * EN: Replaces WoW's TraceLine with a test wall and checks ray share, budget,
 *     smoothing and cleanup.
 */
#include "voicecore/Occlusion.h"

#include <cmath>
#include <cstdio>

using namespace voicecore;
using spatial::Vec3;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

// DE: Wand bei x = 10, Breite |y| <= halfWidth (unendlich hoch). EN: wall at x = 10, width |y| <= halfWidth (infinitely high).
static OcclusionTracker::RayFn Wall(float halfWidth, int* rays)
{
    return [halfWidth, rays](const Vec3& a, const Vec3& b) {
        ++*rays;
        if ((a.x - 10.0f) * (b.x - 10.0f) >= 0.0f) return false;   // kreuzt x = 10 nicht / does not cross x = 10
        float t = (10.0f - a.x) / (b.x - a.x);
        float y = a.y + (b.y - a.y) * t;
        return std::fabs(y) <= halfWidth;
    };
}

int main()
{
    Vec3 me{ 0, 0, 0 };

    // --- Voll verdeckt, frei, teilweise / fully occluded, free, partial ---
    {
        OcclusionTracker t;
        int rays = 0;
        t.SetCandidates({ { 1, { 20, 0, 0 } }, { 2, { 5, 0, 0 } } });
        int used = t.Trace(me, 30, 1000, Wall(100.0f, &rays));
        CHECK(used == 6 && rays == 6);
        CHECK(std::fabs(t.Get(1, 1000) - 1.0f) < 1e-4f);   // hinter der Wand / behind the wall
        CHECK(t.Get(2, 1000) == 0.0f);                      // vor der Wand / in front of the wall
        CHECK(t.Get(3, 1000) == 0.0f);                      // unbekannt / unknown

        OcclusionTracker c;
        rays = 0;
        c.SetCandidates({ { 1, { 20, 0, 0 } } });
        c.Trace(me, 30, 1000, Wall(0.2f, &rays));            // nur der Mittelstrahl trifft / only the centre ray hits
        CHECK(std::fabs(c.Get(1, 1000) - 1.0f / 3.0f) < 1e-4f);
    }

    // --- Budget: 4 Sprecher, 9 Strahlen -> 3 jetzt, der 4. im naechsten Tick ---
    // --- budget: 4 speakers, 9 rays -> 3 now, the 4th on the next tick ---
    {
        OcclusionTracker t;
        int rays = 0;
        t.SetCandidates({ { 1, { 20, 0, 0 } }, { 2, { 20, 5, 0 } }, { 3, { 20, -5, 0 } }, { 4, { 20, 9, 0 } } });
        CHECK(t.Trace(me, 9, 1000, Wall(100.0f, &rays)) == 9);
        CHECK(t.Trace(me, 9, 1010, Wall(100.0f, &rays)) == 3);   // nur der Rest / only the rest
        CHECK(t.Trace(me, 9, 1020, Wall(100.0f, &rays)) == 0);   // noch nicht faellig / not due yet
        CHECK(t.Trace(me, 9, 1120, Wall(100.0f, &rays)) == 9);   // wieder faellig / due again
        for (uint32_t s = 1; s <= 4; ++s) CHECK(t.Get(s, 1120) > 0.99f);
    }

    // --- Glaettung: Wand verschwindet -> faellt weich ab / smoothing: wall disappears -> falls off softly ---
    {
        OcclusionTracker t;
        OcclusionParams p;
        p.smoothingMs = 100.0f;
        p.refreshMs = 0;
        t.SetParams(p);
        int rays = 0;
        t.SetCandidates({ { 1, { 20, 0, 0 } } });
        t.Trace(me, 3, 1000, Wall(100.0f, &rays));
        CHECK(t.Get(1, 1000) > 0.99f);
        t.Trace(me, 3, 1001, Wall(0.0f - 1.0f, &rays));        // keine Wand mehr / no wall anymore
        float a = t.Get(1, 1051), b = t.Get(1, 1300);
        CHECK(a < 0.99f && a > 0.4f);                           // nach 50 ms halb unten / half-way after 50 ms
        CHECK(b < 0.06f);                                       // nach 300 ms fast frei / almost free after 300 ms
    }

    // --- Direkt nebeneinander und Aufraeumen / right next to each other and cleanup ---
    {
        OcclusionTracker t;
        int rays = 0;
        t.SetCandidates({ { 1, { 0.5f, 0, 0 } } });
        CHECK(t.Trace(me, 9, 1000, Wall(100.0f, &rays)) == 0 && rays == 0);
        CHECK(t.Get(1, 1000) == 0.0f);
        t.SetCandidates({});
        CHECK(t.Get(1, 1000) == 0.0f);
    }

    if (g_fail == 0) std::printf("occlusion_test: all checks passed\n");
    return g_fail == 0 ? 0 : 1;
}
