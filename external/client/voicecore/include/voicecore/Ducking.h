/*
 * mod-voicechat - lowering of game sounds while voice is active (pure, testable)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Kanaele (z. B. Ton/Musik/Umgebung) werden weich auf original * Faktor abgesenkt.
 *     Absturzsicher: Vor dem ersten Absenken werden die Originalwerte in eine Datei
 *     geschrieben; findet der naechste Start die Datei, werden sie zurueckgeschrieben.
 *     Aendert der Spieler einen Wert waehrend des Absenkens, gilt er als neuer Normalwert.
 *     Lesen/Schreiben der Werte (bei WoW: CVars) wird als Funktion uebergeben.
 * EN: channels (e.g. sound/music/ambience) are lowered softly to original * factor.
 *     Crash-safe: before the first lowering, the original values are written to a file;
 *     if the next start finds the file, they are written back. If the player changes a
 *     value while lowered, it becomes the new normal value.
 *     Reading/writing the values (WoW: CVars) is passed in as functions.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace voicecore
{
    class Ducking
    {
    public:
        static constexpr int CHANNELS = 3;
        using ReadFn = std::function<bool(int channel, float& value)>;
        using WriteFn = std::function<bool(int channel, float value)>;

        explicit Ducking(std::string stateFile, float fadeMs = 250.0f);

        // DE: beliebiger Thread. factor 1 = unveraendert, 0 = stumm. EN: any thread. factor 1 = unchanged, 0 = muted.
        void SetTarget(bool duck, const float factors[CHANNELS]);

        // DE: regelmaessig im Thread, der die Werte lesen/schreiben darf. active=false -> sofort zurueck.
        // EN: regularly on the thread allowed to read/write the values. active=false -> restore immediately.
        void Tick(bool active, uint64_t nowMs, const ReadFn& read, const WriteFn& write);

        float Level() const { return _level; }        // 0 = normal, 1 = voll abgesenkt / fully lowered
        bool Lowered() const { return _saved; }
        bool RestoredAfterCrash() const { return _restoredAfterCrash; }

    private:
        void SaveFile();
        void Restore(const WriteFn& write);

        std::string _file;
        float _fadeMs;
        std::mutex _mutex;
        bool _want = false;
        float _factor[CHANNELS] = { 1.0f, 1.0f, 1.0f };

        bool _checkedFile = false, _saved = false, _restoredAfterCrash = false;
        float _level = 0.0f;
        float _orig[CHANNELS] = {}, _last[CHANNELS] = {};
        uint64_t _lastTick = 0;
    };
}
