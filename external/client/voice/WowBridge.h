/*
 * mod-voicechat - minimal WoW state access (phase 3)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Liest nur zwei Werte aus dem eigenen Prozess, abgesichert ueber
 *     ReadProcessMemory (kein Absturz bei falscher Adresse). Ab Phase 4 wird
 *     das durch Hooks im Hauptthread ersetzt.
 * EN: Reads only two values from its own process, guarded via
 *     ReadProcessMemory (no crash on a wrong address). From phase 4 on this is
 *     replaced by hooks on the main thread.
 */
#pragma once

#include <cstdint>
#include <string>

namespace voice
{
    class WowBridge
    {
    public:
        void Configure(uintptr_t inWorldAddr, uintptr_t nameAddr) { _inWorld = inWorldAddr; _name = nameAddr; }
        bool InWorld() const;
        std::string CharacterName() const;
        static bool IsForeground();

    private:
        uintptr_t _inWorld = 0;
        uintptr_t _name = 0;
    };
}
