/*
 * mod-voicechat - MVCP link to the worldserver through the WoW connection
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Server -> Client: SMSG_VOICE_SESSION_ADJUST_PRIORITY (0x3A0) mit "MVC1"-Nutzlast.
 *     Unser Handler ersetzt den Original-Handler und reicht alles andere an ihn weiter.
 *     Client -> Server: CMSG_VOICE_SESSION_ENABLE (0x3AF) = [u8 voice][u8 mic] + MVCP.
 *     MainTick() laeuft im WoW-Hauptthread (GameThread), alles andere im Worker.
 * EN: server -> client: SMSG_VOICE_SESSION_ADJUST_PRIORITY (0x3A0) with "MVC1" payload.
 *     Our handler replaces the original one and forwards everything else to it.
 *     client -> server: CMSG_VOICE_SESSION_ENABLE (0x3AF) = [u8 voice][u8 mic] + MVCP.
 *     MainTick() runs on the WoW main thread (GameThread), everything else in the worker.
 */
#pragma once

#include "WowApi.h"

#include <deque>
#include <mutex>
#include <vector>

namespace voice
{
    class ServerLink
    {
    public:
        // DE: Hauptthread. EN: main thread.
        void MainTick(bool inWorld);

        // DE: Worker. EN: worker.
        void Send(std::vector<uint8_t> mvcp);
        bool Poll(std::vector<uint8_t>& mvcp);
        void ClearOutgoing();

    private:
        static int __cdecl Handler(void* param, uint32_t opcode, uint32_t time, void* msg);

        std::mutex _mutex;
        std::deque<std::vector<uint8_t>> _in, _out;
        wow::MessageHandler _orig = nullptr;
        void* _origParam = nullptr;
        bool _logged = false;
    };
}
