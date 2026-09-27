/*
 * mod-voicechat - WoW 3.3.5a client functions (see WowApi.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "WowApi.h"

namespace wow
{
    bool SafeRead(uintptr_t addr, void* out, size_t len)
    {
        SIZE_T got = 0;
        return addr && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(addr), out, len, &got) && got == len;
    }

    uint64_t ActivePlayerGuid()
    {
        return reinterpret_cast<uint64_t(__cdecl*)()>(addr::GetActivePlayer)();
    }

    bool PlayerPosition(C3Vector& pos, float& facing)
    {
        uint64_t guid = ActivePlayerGuid();
        if (!guid) return false;
        auto obj = reinterpret_cast<uintptr_t>(
            reinterpret_cast<void*(__cdecl*)(uint64_t, uint32_t)>(addr::GetObjectPtr)(guid, addr::TypeMaskPlayer));
        if (!obj) return false;
        float v[3];
        if (!SafeRead(obj + addr::UnitPosX, v, sizeof(v)) || !SafeRead(obj + addr::UnitFacing, &facing, sizeof(facing)))
            return false;
        pos = { v[0], v[1], v[2] };
        return true;
    }

    bool CameraPosition(C3Vector& pos)
    {
        auto cam = reinterpret_cast<uintptr_t>(reinterpret_cast<void*(__cdecl*)()>(addr::GetActiveCamera)());
        if (!cam) return false;
        float v[3];
        if (!SafeRead(cam + addr::CameraPos, v, sizeof(v))) return false;
        pos = { v[0], v[1], v[2] };
        return true;
    }

    uint32_t CurrentMapId()
    {
        int32_t id = -1;
        SafeRead(addr::CurrentMapId, &id, sizeof(id));
        return uint32_t(id);
    }

    std::string PlayerName()
    {
        char buf[49] = {};
        if (!SafeRead(addr::PlayerName, buf, 48)) return std::string();
        return std::string(buf);
    }

    HWND MainWindow()
    {
        HWND h = nullptr;
        SafeRead(addr::MainWindow, &h, sizeof(h));
        return IsWindow(h) ? h : nullptr;
    }
}
