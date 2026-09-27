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

    bool Guarded(void (*fn)(void*), void* ctx)
    {
#ifdef _MSC_VER
        __try { fn(ctx); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
        fn(ctx);   // DE: MinGW hat kein __try. EN: MinGW has no __try.
        return true;
#endif
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

    bool TraceLine(const C3Vector& start, const C3Vector& end, uint32_t flags)
    {
        using Fn = bool(__cdecl*)(const C3Vector*, const C3Vector*, C3Vector*, float*, uint32_t, uint32_t);
        C3Vector hit;
        float fraction = 1.0f;
        return reinterpret_cast<Fn>(addr::TraceLine)(&start, &end, &hit, &fraction, flags, 0);
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

    // ---- Netzwerk / network --------------------------------------------------------------------
    namespace
    {
        // DE: CDataStore (12340): {vtable, buffer, base, alloc, size, read}. Lesen: buffer + (read - base).
        // EN: CDataStore (12340): {vtable, buffer, base, alloc, size, read}. Reading: buffer + (read - base).
        struct CDataStore
        {
            uintptr_t vtable;
            uint8_t* buffer;
            uint32_t base, alloc, size, read;
        };
        // DE: __thiscall als __fastcall mit Dummy-EDX (funktioniert mit MSVC und MinGW).
        // EN: __thiscall as __fastcall with a dummy EDX (works with MSVC and MinGW).
        using DsCtor    = void(__fastcall*)(CDataStore*, void*);
        using DsPutU8   = void(__fastcall*)(CDataStore*, void*, uint8_t);
        using DsPutU32  = void(__fastcall*)(CDataStore*, void*, uint32_t);
        using DsRelease = void(__fastcall*)(CDataStore*, void*);
        using SendFn    = void(__cdecl*)(CDataStore*);
        using SetHandlerFn = void(__cdecl*)(uint32_t, MessageHandler, void*);

        uintptr_t Connection()
        {
            uintptr_t c = 0;
            SafeRead(addr::ClientConnection, &c, sizeof(c));
            return c;
        }
    }

    bool NetConnected() { return Connection() != 0; }

    MessageHandler GetMessageHandler(uint32_t opcode, void** param)
    {
        uintptr_t c = Connection();
        MessageHandler fn = nullptr;
        if (!c || opcode >= addr::NetMaxOpcode) return nullptr;
        SafeRead(c + addr::NetHandlers + opcode * 4, &fn, sizeof(fn));
        if (param) SafeRead(c + addr::NetHandlerParams + opcode * 4, param, sizeof(void*));
        return fn;
    }

    bool SetMessageHandler(uint32_t opcode, MessageHandler fn, void* param)
    {
        // DE: 0x6B0B80 bricht bei fehlender Verbindung/Handler mit Fatal Error ab -> vorher pruefen.
        // EN: 0x6B0B80 aborts with a fatal error without connection/handler -> check first.
        if (!fn || !Connection() || opcode >= addr::NetMaxOpcode) return false;
        reinterpret_cast<SetHandlerFn>(addr::ClientSetMsgHandler)(opcode, fn, param);
        return true;
    }

    bool SendPacket(uint32_t opcode, const uint8_t* data, size_t len)
    {
        if (!Connection()) return false;   // DE: sonst Fatal Error. EN: fatal error otherwise.
        CDataStore ds;
        reinterpret_cast<DsCtor>(addr::DataStoreGenPacket)(&ds, nullptr);
        reinterpret_cast<DsPutU32>(addr::DataStorePutInt32)(&ds, nullptr, opcode);
        for (size_t i = 0; i < len; ++i)
            reinterpret_cast<DsPutU8>(addr::DataStorePutInt8)(&ds, nullptr, data[i]);
        ds.read = 0;   // Finalize
        reinterpret_cast<SendFn>(addr::ClientSendPacket)(&ds);
        reinterpret_cast<DsRelease>(addr::DataStoreRelease)(&ds, nullptr);
        return true;
    }

    bool PeekPacket(void* msg, const uint8_t*& data, size_t& len)
    {
        auto* ds = static_cast<CDataStore*>(msg);
        if (!ds || !ds->buffer || ds->read > ds->size || ds->read < ds->base) return false;
        data = ds->buffer + (ds->read - ds->base);
        len = ds->size - ds->read;
        return true;
    }

    void ConsumePacket(void* msg)
    {
        auto* ds = static_cast<CDataStore*>(msg);
        if (ds) ds->read = ds->size;
    }

    HWND MainWindow()
    {
        HWND h = nullptr;
        SafeRead(addr::MainWindow, &h, sizeof(h));
        return IsWindow(h) ? h : nullptr;
    }
}
