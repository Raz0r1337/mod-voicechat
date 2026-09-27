/*
 * mod-voicechat - minimal WoW state access (see WowBridge.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "WowBridge.h"

#include <windows.h>

namespace voice
{
    namespace
    {
        bool SafeRead(uintptr_t addr, void* out, size_t len)
        {
            SIZE_T got = 0;
            return addr && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(addr), out, len, &got) && got == len;
        }
    }

    bool WowBridge::InWorld() const
    {
        uint8_t v = 0;
        return SafeRead(_inWorld, &v, 1) && v == 1;
    }

    std::string WowBridge::CharacterName() const
    {
        char buf[49] = {};
        if (!SafeRead(_name, buf, 48)) return std::string();
        return std::string(buf);   // UTF-8, nullterminiert / null-terminated
    }

    bool WowBridge::IsForeground()
    {
        HWND w = GetForegroundWindow();
        DWORD pid = 0;
        if (w) GetWindowThreadProcessId(w, &pid);
        return pid == GetCurrentProcessId();
    }
}
