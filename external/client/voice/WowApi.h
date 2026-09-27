/*
 * mod-voicechat - WoW 3.3.5a (build 12340) client functions and addresses
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Alle Adressen wurden statisch an der originalen 12340-Exe geprueft
 *     (Funktionsanfang/Verwendung passt), aber noch NICHT zur Laufzeit.
 *     Quellen: WotLK-Extensions (MIT), wow3.dll, eigene Disassembly.
 *     WICHTIG: Objektmanager-Funktionen nutzen Thread-Local-Storage und
 *     duerfen NUR im WoW-Hauptthread aufgerufen werden (siehe GameThread).
 * EN: All addresses were checked statically against the original 12340 exe
 *     (function prologue/usage matches), but NOT at runtime yet.
 *     Sources: WotLK-Extensions (MIT), wow3.dll, own disassembly.
 *     IMPORTANT: object manager functions use thread-local storage and must
 *     ONLY be called on the WoW main thread (see GameThread).
 */
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace wow
{
    namespace addr
    {
        constexpr uintptr_t GetActivePlayer       = 0x004D3790;  // uint64 __cdecl()           (TLS-Objektmanager / TLS object manager)
        constexpr uintptr_t GetObjectPtr          = 0x004D4DB0;  // void* __cdecl(uint64, uint32)
        constexpr uintptr_t GetActiveCamera       = 0x004F5960;  // CCamera* __cdecl()          ([[0xB7436C]+0x7E20])
        constexpr uintptr_t ClientSetMsgHandler   = 0x006B0B80;  // void __cdecl(opcode, handler, param)  (Phase 5)
        constexpr uintptr_t ClientSendPacket      = 0x006B0B50;  // void __cdecl(CDataStore*)              (Phase 5)
        constexpr uintptr_t DataStoreGenPacket    = 0x00401050;  // __thiscall                              (Phase 5)
        constexpr uintptr_t DataStorePutInt8      = 0x0047AFE0;  // __thiscall(uint8)                       (Phase 5)
        constexpr uintptr_t DataStorePutInt32     = 0x0047B0A0;  // __thiscall(uint32)                      (Phase 5)
        constexpr uintptr_t DataStoreRelease      = 0x00403880;  // __thiscall                              (Phase 5)
        constexpr uintptr_t TraceLine             = 0x007A3B70;  // bool __cdecl(start*, end*, hit*, dist*, flags, 0) (Phase 6)
        constexpr uintptr_t FrameScriptRegister   = 0x00817F90;  // __cdecl(name, fn)                       (Phase 7)
        constexpr uintptr_t FrameScriptExecute    = 0x00819210;  // __cdecl(code, source, 0)                (Phase 7)
        constexpr uintptr_t FrameScriptSignal     = 0x0081B530;  // __cdecl(eventId, fmt, ...)              (Phase 7)

        constexpr uintptr_t MainWindow            = 0x00D41620;  // HWND (auch in St0ny's FlashWindow-Patch / also used by)
        constexpr uintptr_t CurrentMapId          = 0x00BD088C;  // int32 (WotLK-Extensions; wow3.dll nutzt 0xAB63BC)
        constexpr uintptr_t PlayerName            = 0x00C79D18;  // char[] (wow3.dll)

        constexpr uint32_t  UnitPosX              = 0x798;       // float x,y,z (+0x798..0x7A0)
        constexpr uint32_t  UnitFacing            = 0x7A8;       // float
        constexpr uint32_t  CameraPos             = 0x08;        // float x,y,z
        constexpr uint32_t  TypeMaskPlayer        = 0x10;
    }

    struct C3Vector { float x = 0, y = 0, z = 0; };

    // DE: Nur im Hauptthread! EN: main thread only!
    uint64_t ActivePlayerGuid();
    bool PlayerPosition(C3Vector& pos, float& facing);
    bool CameraPosition(C3Vector& pos);

    // DE: Ueberall nutzbar (abgesichertes Lesen). EN: usable anywhere (guarded reads).
    bool SafeRead(uintptr_t addr, void* out, size_t len);
    uint32_t CurrentMapId();
    std::string PlayerName();
    HWND MainWindow();
}
