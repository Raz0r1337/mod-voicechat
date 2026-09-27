/*
 * mod-voicechat - MVCP link to the worldserver (see ServerLink.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "ServerLink.h"
#include "Config.h"

#include "VoiceProtocol.h"

#include <cstring>

namespace voice
{
    void ServerLink::MainTick(bool inWorld)
    {
        if (!inWorld || !wow::NetConnected())
            return;

        // DE: Handler (neu) setzen, falls WoW ihn ueberschrieben hat oder die Verbindung neu ist.
        // EN: (re)install the handler if WoW overwrote it or the connection is new.
        void* param = nullptr;
        wow::MessageHandler cur = wow::GetMessageHandler(VoiceProto::SMSG_OPCODE, &param);
        if (cur != &ServerLink::Handler)
        {
            _orig = cur;
            _origParam = param;
            if (wow::SetMessageHandler(VoiceProto::SMSG_OPCODE, &ServerLink::Handler, this) && !_logged)
            {
                Log("ServerLink: SMSG handler installed");
                _logged = true;
            }
        }

        std::deque<std::vector<uint8_t>> out;
        {
            std::lock_guard<std::mutex> g(_mutex);
            out.swap(_out);
        }
        for (auto& m : out)
        {
            std::vector<uint8_t> pkt = { 1, 1 };   // voice enabled, mic enabled
            pkt.insert(pkt.end(), m.begin(), m.end());
            wow::SendPacket(VoiceProto::CMSG_OPCODE, pkt.data(), pkt.size());
        }
    }

    int __cdecl ServerLink::Handler(void* param, uint32_t opcode, uint32_t time, void* msg)
    {
        auto* self = static_cast<ServerLink*>(param);
        const uint8_t* data = nullptr;
        size_t len = 0;
        if (self && wow::PeekPacket(msg, data, len) && len >= 5 && std::memcmp(data, VoiceProto::MAGIC, 4) == 0)
        {
            {
                std::lock_guard<std::mutex> g(self->_mutex);
                if (self->_in.size() < 256)
                    self->_in.emplace_back(data, data + len);
            }
            wow::ConsumePacket(msg);
            return 1;
        }
        // DE: Kein MVCP -> Original-Handler. EN: not MVCP -> original handler.
        if (self && self->_orig)
            return self->_orig(self->_origParam, opcode, time, msg);
        wow::ConsumePacket(msg);
        return 1;
    }

    void ServerLink::Send(std::vector<uint8_t> mvcp)
    {
        std::lock_guard<std::mutex> g(_mutex);
        if (_out.size() < 16)
            _out.push_back(std::move(mvcp));
    }

    bool ServerLink::Poll(std::vector<uint8_t>& mvcp)
    {
        std::lock_guard<std::mutex> g(_mutex);
        if (_in.empty()) return false;
        mvcp = std::move(_in.front());
        _in.pop_front();
        return true;
    }

    void ServerLink::ClearOutgoing()
    {
        std::lock_guard<std::mutex> g(_mutex);
        _out.clear();
    }
}
