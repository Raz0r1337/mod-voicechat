/*
 * mod-voicechat - occlusion tracker (pure, testable, no WoW dependencies)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Plant Sichtstrahlen vom Kopf des Hoerers zu jedem Sprecher (Mitte, links,
 *     rechts versetzt) mit festem Budget pro Tick. Anteil blockierter Strahlen =
 *     Verdeckung 0..1 (weich, z. B. an Ecken), zeitlich geglaettet.
 *     Der eigentliche Strahltest (WoW TraceLine) wird als Funktion uebergeben und
 *     laeuft im WoW-Hauptthread; Get() wird vom Audio-Thread gelesen.
 * EN: Schedules line-of-sight rays from the listener's head to every speaker
 *     (centre, left, right offset) with a fixed budget per tick. Share of blocked
 *     rays = occlusion 0..1 (soft, e.g. at corners), smoothed over time.
 *     The actual ray test (WoW TraceLine) is passed in as a function and runs on
 *     the WoW main thread; Get() is read by the audio thread.
 */
#pragma once

#include "Spatial.h"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace voicecore
{
    struct OcclusionParams
    {
        float headHeight = 1.5f;      // Yards ueber den Fuessen / yards above the feet
        float sideOffset = 0.7f;      // Yards, seitliche Strahlen am Sprecher / side rays at the speaker
        unsigned refreshMs = 100;     // je Sprecher / per speaker
        float smoothingMs = 120.0f;   // Zeitkonstante / time constant
    };

    class OcclusionTracker
    {
    public:
        static constexpr int RAYS_PER_SPEAKER = 3;
        // DE: true = Strahl blockiert. EN: true = ray blocked.
        using RayFn = std::function<bool(const spatial::Vec3& from, const spatial::Vec3& to)>;

        void SetParams(const OcclusionParams& p);

        // DE: Worker: Sprecher, fuer die Verdeckung gilt (WoW-Koordinaten der Fuesse). Ersetzt die Liste.
        // EN: worker: speakers occlusion applies to (WoW coordinates of the feet). Replaces the list.
        void SetCandidates(const std::vector<std::pair<uint32_t, spatial::Vec3>>& speakers);

        // DE: Hauptthread: max. maxRays Strahlen, faelligste Sprecher zuerst. Liefert die Zahl der Strahlen.
        // EN: main thread: at most maxRays rays, most overdue speakers first. Returns the number of rays.
        int Trace(const spatial::Vec3& listenerFeet, int maxRays, uint64_t nowMs, const RayFn& ray);

        // DE: Audio-Thread: geglaettete Verdeckung 0..1. EN: audio thread: smoothed occlusion 0..1.
        float Get(uint32_t session, uint64_t nowMs);

        void Clear();

    private:
        struct Entry
        {
            spatial::Vec3 pos;
            float target = 0.0f, value = 0.0f;
            uint64_t lastTrace = 0, lastValue = 0;
            bool traced = false;
        };

        std::mutex _mutex;
        OcclusionParams _p;
        std::map<uint32_t, Entry> _entries;
    };
}
