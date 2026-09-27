/*
 * mod-voicechat - lower game sounds while voice is active (phase 8b)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: WoW-Anbindung fuer voicecore::Ducking: Kanaele = Sound_SFXVolume / Sound_MusicVolume /
 *     Sound_AmbienceVolume, gelesen ueber CVar::Lookup, geschrieben ueber CVar::Set (wie Lua
 *     SetCVar), im WoW-Hauptthread unter SEH-Schutz. Sicherungsdatei: voice.duck neben Wow.exe.
 * EN: WoW binding for voicecore::Ducking: channels = Sound_SFXVolume / Sound_MusicVolume /
 *     Sound_AmbienceVolume, read via CVar::Lookup, written via CVar::Set (like Lua SetCVar),
 *     on the WoW main thread under the SEH guard. Backup file: voice.duck next to Wow.exe.
 */
#pragma once

#include "voicecore/Ducking.h"

namespace voice
{
    class GameDucking
    {
    public:
        GameDucking();

        // DE: Worker. EN: worker.
        void SetTarget(bool duck, float sfx, float music, float ambience);

        // DE: Hauptthread, jeder Tick (auch ausserhalb der Welt). EN: main thread, every tick (also outside the world).
        void MainTick(bool inWorld);

    private:
        voicecore::Ducking _core;
        bool _broken = false;
        bool _loggedRestore = false;
    };
}
