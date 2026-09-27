/*
 * mod-voicechat - unit tests for voicecore::Ducking (no WoW needed)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Simuliert WoWs Lautstaerke-CVars und prueft Ueberblenden, Wiederherstellen,
 *     Reglerbewegung waehrend des Absenkens und die Wiederherstellung nach einem Absturz.
 * EN: Simulates WoW's volume CVars and checks fading, restoring, slider movement while
 *     lowered and recovery after a crash.
 */
#include "voicecore/Ducking.h"

#include <cmath>
#include <cstdio>

using voicecore::Ducking;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

struct FakeCVars
{
    float v[3] = { 0.8f, 0.6f, 1.0f };
    int writes = 0;
    Ducking::ReadFn Read() { return [this](int i, float& out) { out = v[i]; return true; }; }
    Ducking::WriteFn Write() { return [this](int i, float x) { v[i] = x; ++writes; return true; }; }
};

static bool Near(float a, float b) { return std::fabs(a - b) < 0.006f; }
static bool FileExists(const char* p) { if (FILE* f = std::fopen(p, "r")) { std::fclose(f); return true; } return false; }

int main()
{
    const char* file = "ducking_test.state";
    std::remove(file);
    const float half[3] = { 0.5f, 0.25f, 1.0f };

    // --- Absenken + weiches Zurueckblenden / lowering + soft return ---
    {
        FakeCVars w;
        Ducking d(file, 250.0f);
        d.Tick(true, 1000, w.Read(), w.Write());
        CHECK(w.writes == 0 && !FileExists(file));             // nichts zu tun / nothing to do
        d.SetTarget(true, half);
        uint64_t t = 1000;
        for (int i = 0; i < 20; ++i) d.Tick(true, t += 20, w.Read(), w.Write());
        CHECK(d.Level() == 1.0f && d.Lowered() && FileExists(file));
        CHECK(Near(w.v[0], 0.4f) && Near(w.v[1], 0.15f) && Near(w.v[2], 1.0f));
        d.SetTarget(false, half);
        d.Tick(true, t += 100, w.Read(), w.Write());
        CHECK(w.v[0] > 0.4f && w.v[0] < 0.8f);                  // blendet noch / still fading
        for (int i = 0; i < 20; ++i) d.Tick(true, t += 20, w.Read(), w.Write());
        CHECK(!d.Lowered() && !FileExists(file));
        CHECK(Near(w.v[0], 0.8f) && Near(w.v[1], 0.6f) && Near(w.v[2], 1.0f));
    }

    // --- Regler waehrend des Absenkens bewegt / slider moved while lowered ---
    {
        FakeCVars w;
        Ducking d(file, 0.0f);                                   // ohne Ueberblenden / without fading
        d.SetTarget(true, half);
        d.Tick(true, 1000, w.Read(), w.Write());
        CHECK(Near(w.v[0], 0.4f));
        w.v[0] = 0.3f;                                           // Spieler / player
        d.Tick(true, 1020, w.Read(), w.Write());
        CHECK(Near(w.v[0], 0.15f));                              // neuer Normalwert 0.3 * 0.5 / new normal 0.3 * 0.5
        d.SetTarget(false, half);
        d.Tick(true, 1040, w.Read(), w.Write());
        CHECK(Near(w.v[0], 0.3f) && Near(w.v[1], 0.6f));        // zurueck auf den neuen Normalwert / back to the new normal
    }

    // --- Ausloggen stellt sofort her / logging out restores immediately ---
    {
        FakeCVars w;
        Ducking d(file, 0.0f);
        d.SetTarget(true, half);
        d.Tick(true, 1000, w.Read(), w.Write());
        d.Tick(false, 1020, w.Read(), w.Write());
        CHECK(Near(w.v[0], 0.8f) && !d.Lowered() && !FileExists(file));
    }

    // --- Absturz waehrend des Absenkens / crash while lowered ---
    {
        FakeCVars w;
        {
            Ducking d(file, 0.0f);
            d.SetTarget(true, half);
            d.Tick(true, 1000, w.Read(), w.Write());
            CHECK(FileExists(file) && Near(w.v[0], 0.4f));
            // DE: Prozess stirbt hier, WoW speichert 0.4 in Config.wtf. EN: process dies here, WoW saves 0.4.
        }
        FakeCVars next;
        next.v[0] = 0.4f; next.v[1] = 0.15f; next.v[2] = 1.0f;   // Werte nach dem Neustart / values after restart
        Ducking d2(file, 0.0f);
        d2.Tick(false, 5000, next.Read(), next.Write());         // auch im Login-Bildschirm / also at the login screen
        CHECK(d2.RestoredAfterCrash() && !FileExists(file));
        CHECK(Near(next.v[0], 0.8f) && Near(next.v[1], 0.6f) && Near(next.v[2], 1.0f));
    }

    // --- Lesen schlaegt fehl -> nie absenken / read fails -> never lower ---
    {
        FakeCVars w;
        Ducking d(file, 0.0f);
        d.SetTarget(true, half);
        d.Tick(true, 1000, [](int, float&) { return false; }, w.Write());
        CHECK(w.writes == 0 && !d.Lowered() && !FileExists(file));
    }

    std::remove(file);
    if (g_fail == 0) std::printf("ducking_test: all checks passed\n");
    return g_fail == 0 ? 0 : 1;
}
