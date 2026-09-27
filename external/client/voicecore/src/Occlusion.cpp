/*
 * mod-voicechat - occlusion tracker (see Occlusion.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "voicecore/Occlusion.h"

#include <algorithm>
#include <cmath>

namespace voicecore
{
    void OcclusionTracker::SetParams(const OcclusionParams& p)
    {
        std::lock_guard<std::mutex> g(_mutex);
        _p = p;
    }

    void OcclusionTracker::SetCandidates(const std::vector<std::pair<uint32_t, spatial::Vec3>>& speakers)
    {
        std::lock_guard<std::mutex> g(_mutex);
        std::map<uint32_t, Entry> next;
        for (const auto& s : speakers)
        {
            auto it = _entries.find(s.first);
            Entry e = it != _entries.end() ? it->second : Entry();
            e.pos = s.second;
            next[s.first] = e;
        }
        _entries.swap(next);   // DE: Nicht mehr gelistete fallen weg. EN: no longer listed ones are dropped.
    }

    int OcclusionTracker::Trace(const spatial::Vec3& listenerFeet, int maxRays, uint64_t nowMs, const RayFn& ray)
    {
        struct Job { uint32_t session; spatial::Vec3 pos; uint64_t lastTrace; };
        std::vector<Job> jobs;
        OcclusionParams p;
        {
            std::lock_guard<std::mutex> g(_mutex);
            p = _p;
            for (const auto& kv : _entries)
                if (!kv.second.traced || nowMs - kv.second.lastTrace >= p.refreshMs)
                    jobs.push_back({ kv.first, kv.second.pos, kv.second.traced ? kv.second.lastTrace : 0 });
        }
        // DE: Faelligste zuerst (noch nie getestete ganz vorn). EN: most overdue first (never traced at the very front).
        std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.lastTrace < b.lastTrace; });

        int used = 0;
        std::vector<std::pair<uint32_t, float>> results;
        spatial::Vec3 from{ listenerFeet.x, listenerFeet.y, listenerFeet.z + p.headHeight };
        for (const Job& j : jobs)
        {
            if (used + RAYS_PER_SPEAKER > maxRays) break;
            spatial::Vec3 to{ j.pos.x, j.pos.y, j.pos.z + p.headHeight };
            float dx = to.x - from.x, dy = to.y - from.y;
            float planar = std::sqrt(dx * dx + dy * dy);
            if (planar < 1.0f)
            {
                // DE: Direkt nebeneinander -> nie verdeckt. EN: right next to each other -> never occluded.
                results.emplace_back(j.session, 0.0f);
                continue;
            }
            // DE: Seitliche Versetzung senkrecht zur Blicklinie. EN: side offset perpendicular to the line of sight.
            float px = -dy / planar * p.sideOffset, py = dx / planar * p.sideOffset;
            const spatial::Vec3 targets[RAYS_PER_SPEAKER] = {
                to,
                { to.x + px, to.y + py, to.z },
                { to.x - px, to.y - py, to.z },
            };
            int blocked = 0;
            for (const auto& t : targets)
                if (ray(from, t)) ++blocked;
            used += RAYS_PER_SPEAKER;
            results.emplace_back(j.session, float(blocked) / float(RAYS_PER_SPEAKER));
        }

        std::lock_guard<std::mutex> g(_mutex);
        for (const auto& r : results)
        {
            auto it = _entries.find(r.first);
            if (it == _entries.end()) continue;   // DE: inzwischen entfernt / removed meanwhile
            Entry& e = it->second;
            e.target = r.second;
            e.lastTrace = nowMs;
            if (!e.traced)
            {
                // DE: Erstes Ergebnis sofort, damit ein neuer Sprecher hinter der Wand nicht erst laut ist.
                // EN: first result immediately so a new speaker behind a wall is not loud at first.
                e.value = e.target;
                e.lastValue = nowMs;
                e.traced = true;
            }
        }
        return used;
    }

    float OcclusionTracker::Get(uint32_t session, uint64_t nowMs)
    {
        std::lock_guard<std::mutex> g(_mutex);
        auto it = _entries.find(session);
        if (it == _entries.end() || !it->second.traced) return 0.0f;
        Entry& e = it->second;
        if (nowMs > e.lastValue)
        {
            float dt = float(nowMs - e.lastValue);
            float k = _p.smoothingMs > 0.0f ? 1.0f - std::exp(-dt / _p.smoothingMs) : 1.0f;
            e.value += (e.target - e.value) * k;
            e.lastValue = nowMs;
        }
        return std::clamp(e.value, 0.0f, 1.0f);
    }

    void OcclusionTracker::Clear()
    {
        std::lock_guard<std::mutex> g(_mutex);
        _entries.clear();
    }
}
