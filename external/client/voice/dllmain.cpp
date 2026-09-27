/*
 * mod-voicechat - voice.dll entry point
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: DllMain macht so wenig wie moeglich (Loader-Lock!): nur einen Thread
 *     starten. Die DLL wird vom Exe-Patch sehr frueh geladen (vor der CRT
 *     von Wow.exe); der Thread wartet daher kurz, bevor er arbeitet.
 * EN: DllMain does as little as possible (loader lock!): it only starts a
 *     thread. The DLL is loaded very early by the exe patch (before Wow.exe's
 *     CRT); the thread therefore waits briefly before doing any work.
 */
#include "Config.h"
#include "VoiceApp.h"

#include <windows.h>

namespace
{
    voice::VoiceApp* g_app = nullptr;

    DWORD WINAPI VoiceThread(LPVOID)
    {
        Sleep(2000);
        try
        {
            g_app->Run();
        }
        catch (...)
        {
            voice::Log("voice thread: unexpected exception - voice disabled");
        }
        return 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinst);
        g_app = new voice::VoiceApp();
        HANDLE t = CreateThread(nullptr, 0, VoiceThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    else if (reason == DLL_PROCESS_DETACH && reserved == nullptr && g_app)
    {
        // DE: nur bei FreeLibrary; bei Prozessende nichts tun. EN: only on FreeLibrary; do nothing on process exit.
        g_app->RequestStop();
    }
    return TRUE;
}
