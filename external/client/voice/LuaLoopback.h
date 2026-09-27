/*
 * mod-voicechat - microphone test functions for the Blizzard voice menu (phase 8c)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Registriert C-Funktionen unter den Namen, die das Voice-Menue aufruft
 *     (VoiceChat_RecordLoopbackSound usw.), und leitet sie an voicecore::LoopbackTest.
 * EN: Registers C functions under the names the voice menu calls
 *     (VoiceChat_RecordLoopbackSound etc.) and forwards them to voicecore::LoopbackTest.
 */
#pragma once

#include "voicecore/LoopbackTest.h"

namespace voice
{
    void BindLoopback(voicecore::LoopbackTest* test);
    // DE: Hauptthread, unter Guarded aufrufen (nach /reload erneut). EN: main thread, call under Guarded (again after /reload).
    void RegisterLoopbackFunctions(void* unused);
}
