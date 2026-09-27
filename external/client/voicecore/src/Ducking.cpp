/*
 * mod-voicechat - lowering of game sounds while voice is active (see Ducking.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "voicecore/Ducking.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace voicecore
{
    namespace
    {
        float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
        // DE: Wie gespeichert (3 Nachkommastellen). EN: as stored (3 decimals).
        float Quantize(float v) { return std::round(Clamp01(v) * 1000.0f) / 1000.0f; }
    }

    Ducking::Ducking(std::string stateFile, float fadeMs) : _file(std::move(stateFile)), _fadeMs(fadeMs) { }

    void Ducking::SetTarget(bool duck, const float factors[CHANNELS])
    {
        std::lock_guard<std::mutex> g(_mutex);
        _want = duck;
        for (int i = 0; i < CHANNELS; ++i) _factor[i] = Clamp01(factors[i]);
    }

    void Ducking::SaveFile()
    {
        if (FILE* f = std::fopen(_file.c_str(), "w"))
        {
            std::fprintf(f, "%.3f %.3f %.3f\n", double(_orig[0]), double(_orig[1]), double(_orig[2]));
            std::fclose(f);
        }
    }

    void Ducking::Restore(const WriteFn& write)
    {
        for (int i = 0; i < CHANNELS; ++i)
        {
            write(i, _orig[i]);
            _last[i] = _orig[i];
        }
        std::remove(_file.c_str());
        _saved = false;
        _level = 0.0f;
    }

    void Ducking::Tick(bool active, uint64_t nowMs, const ReadFn& read, const WriteFn& write)
    {
        float dt = _lastTick ? float(std::min<uint64_t>(nowMs - _lastTick, 200)) : 0.0f;
        _lastTick = nowMs;

        // DE: Einmal: Reste eines Absturzes beseitigen. EN: once: clean up after a crash.
        if (!_checkedFile)
        {
            _checkedFile = true;
            if (FILE* f = std::fopen(_file.c_str(), "r"))
            {
                float a[CHANNELS];
                bool ok = std::fscanf(f, "%f %f %f", &a[0], &a[1], &a[2]) == CHANNELS;
                std::fclose(f);
                if (ok)
                {
                    for (int i = 0; i < CHANNELS; ++i) _orig[i] = Clamp01(a[i]);
                    Restore(write);
                    _restoredAfterCrash = true;
                }
                else
                    std::remove(_file.c_str());
            }
        }

        bool want;
        float factor[CHANNELS];
        {
            std::lock_guard<std::mutex> g(_mutex);
            want = _want;
            for (int i = 0; i < CHANNELS; ++i) factor[i] = _factor[i];
        }
        if (!active)
        {
            if (_saved) Restore(write);
            return;
        }

        float step = _fadeMs > 0.0f ? dt / _fadeMs : 1.0f;
        float target = want ? 1.0f : 0.0f;
        _level = _level < target ? std::min(target, _level + step) : std::max(target, _level - step);

        if (_level > 0.0f && !_saved)
        {
            // DE: Ohne Originalwerte nie absenken. EN: never lower without original values.
            for (int i = 0; i < CHANNELS; ++i)
                if (!read(i, _orig[i])) { _level = 0.0f; return; }
            for (int i = 0; i < CHANNELS; ++i) { _orig[i] = Clamp01(_orig[i]); _last[i] = _orig[i]; }
            SaveFile();
            _saved = true;
        }
        if (!_saved) return;

        bool changed = false;
        for (int i = 0; i < CHANNELS; ++i)
        {
            float cur;
            if (read(i, cur) && std::fabs(cur - _last[i]) > 0.02f)
            {
                // DE: Spieler hat den Regler bewegt -> neuer Normalwert. EN: player moved the slider -> new normal value.
                _orig[i] = Clamp01(cur);
                _last[i] = _orig[i];
                changed = true;
            }
            float desired = Quantize(_orig[i] * (1.0f - _level * (1.0f - factor[i])));
            if (std::fabs(desired - _last[i]) > 0.004f && write(i, desired)) _last[i] = desired;
        }
        if (changed) SaveFile();
        if (_level <= 0.0f) Restore(write);
    }
}
