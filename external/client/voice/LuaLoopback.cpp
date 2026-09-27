/*
 * mod-voicechat - microphone test functions for the Blizzard voice menu (see LuaLoopback.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "LuaLoopback.h"
#include "WowApi.h"

namespace voice
{
    namespace
    {
        voicecore::LoopbackTest* g_test = nullptr;

        int __cdecl Record(void* L)
        {
            double seconds = wow::LuaArgCount(L) >= 1 ? wow::LuaNumberArg(L, 1) : 5.0;
            if (g_test) g_test->StartRecording(seconds > 0.0 ? seconds : 5.0);
            return 0;
        }
        int __cdecl StopRecording(void*) { if (g_test) g_test->StopRecording(); return 0; }
        int __cdecl Play(void*) { if (g_test) g_test->StartPlayback(); return 0; }
        int __cdecl StopPlaying(void*) { if (g_test) g_test->StopPlayback(); return 0; }
        // DE: Das Menue vergleicht mit 0 (Zahl, kein Boolean). EN: the menu compares with 0 (number, not boolean).
        int __cdecl IsRecording(void* L) { wow::LuaPushNumber(L, g_test && g_test->IsRecording() ? 1.0 : 0.0); return 1; }
        int __cdecl IsPlaying(void* L) { wow::LuaPushNumber(L, g_test && g_test->IsPlaying() ? 1.0 : 0.0); return 1; }
        int __cdecl Level(void* L) { wow::LuaPushNumber(L, g_test ? double(g_test->Level()) : 0.0); return 1; }
    }

    void BindLoopback(voicecore::LoopbackTest* test) { g_test = test; }

    void RegisterLoopbackFunctions(void*)
    {
        wow::RegisterLuaFunction("VoiceChat_RecordLoopbackSound", &Record);
        wow::RegisterLuaFunction("VoiceChat_StopRecordingLoopbackSound", &StopRecording);
        wow::RegisterLuaFunction("VoiceChat_PlayLoopbackSound", &Play);
        wow::RegisterLuaFunction("VoiceChat_StopPlayingLoopbackSound", &StopPlaying);
        wow::RegisterLuaFunction("VoiceChat_IsRecordingLoopbackSound", &IsRecording);
        wow::RegisterLuaFunction("VoiceChat_IsPlayingLoopbackSound", &IsPlaying);
        wow::RegisterLuaFunction("VoiceChat_GetCurrentMicrophoneSignalLevel", &Level);
    }
}
