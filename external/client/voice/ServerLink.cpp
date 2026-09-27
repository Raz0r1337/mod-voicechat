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
    namespace
    {
        struct PacketOut { const uint8_t* data; size_t len; };
        struct PacketIn { void* msg; std::vector<uint8_t>* out; bool mvcp; };
    }

    void ServerLink::InstallHandler(void* p)
    {
        auto* self = static_cast<ServerLink*>(p);
        void* param = nullptr;
        wow::MessageHandler cur = wow::GetMessageHandler(VoiceProto::SMSG_OPCODE, &param);
        if (cur == &ServerLink::Handler) return;
        self->_orig = cur;
        self->_origParam = param;
        wow::SetMessageHandler(VoiceProto::SMSG_OPCODE, &ServerLink::Handler, self);
    }

    void ServerLink::SendOne(void* p)
    {
        auto* pkt = static_cast<PacketOut*>(p);
        wow::SendPacket(VoiceProto::CMSG_OPCODE, pkt->data, pkt->len);
    }

    void ServerLink::ReadMvcp(void* p)
    {
        auto* in = static_cast<PacketIn*>(p);
        const uint8_t* data = nullptr;
        size_t len = 0;
        in->mvcp = wow::PeekPacket(in->msg, data, len) && len >= 5 && len <= 65536 &&
                   std::memcmp(data, VoiceProto::MAGIC, 4) == 0;
        if (in->mvcp)
        {
            in->out->assign(data, data + len);
            wow::ConsumePacket(in->msg);
        }
    }

    void ServerLink::MainTick(bool inWorld)
    {
        if (_broken || !inWorld || !wow::NetConnected())
            return;

        // DE: Handler (neu) setzen, falls WoW ihn ueberschrieben hat oder die Verbindung neu ist.
        // EN: (re)install the handler if WoW overwrote it or the connection is new.
        if (!wow::Guarded(&ServerLink::InstallHandler, this))
        {
            _broken = true;
            Log("ServerLink: access violation while installing the handler - server integration disabled");
            return;
        }
        if (!_logged && wow::GetMessageHandler(VoiceProto::SMSG_OPCODE, nullptr) == &ServerLink::Handler)
        {
            Log("ServerLink: SMSG handler installed");
            _logged = true;
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
            PacketOut po{ pkt.data(), pkt.size() };
            if (!wow::Guarded(&ServerLink::SendOne, &po))
            {
                _broken = true;
                Log("ServerLink: access violation while sending - server integration disabled");
                return;
            }
        }
    }

    int __cdecl ServerLink::Handler(void* param, uint32_t opcode, uint32_t time, void* msg)
    {
        auto* self = static_cast<ServerLink*>(param);
        std::vector<uint8_t> data;
        PacketIn in{ msg, &data, false };
        if (self && !self->_broken && !wow::Guarded(&ServerLink::ReadMvcp, &in))
        {
            self->_broken = true;
            Log("ServerLink: access violation while reading a packet - server integration disabled");
        }
        else if (self && in.mvcp)
        {
            std::lock_guard<std::mutex> g(self->_mutex);
            if (self->_in.size() < 256)
                self->_in.push_back(std::move(data));
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
