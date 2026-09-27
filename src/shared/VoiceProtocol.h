/*
 * mod-voicechat - MVCP: control protocol between AzerothCore and voice.dll (header-only)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Laeuft ueber die bestehende WoW-Verbindung, ohne Core-Aenderung:
 *     Client -> Server: CMSG_VOICE_SESSION_ENABLE (0x3AF) = [u8 voice][u8 mic] + MVCP
 *                       (der originale Client sendet nur die 2 Bytes)
 *     Server -> Client: SMSG_VOICE_SESSION_ADJUST_PRIORITY (0x3A0) = MVCP
 *                       (voice.dll ersetzt den Handler; der Server sendet erst nach HELLO)
 *     MVCP = "MVC1" + u8 Typ + Protobuf-Felder (MumbleProtocol.h-Codec).
 * EN: Runs over the existing WoW connection, no core change:
 *     client -> server: CMSG_VOICE_SESSION_ENABLE (0x3AF) = [u8 voice][u8 mic] + MVCP
 *                       (the stock client only sends the 2 bytes)
 *     server -> client: SMSG_VOICE_SESSION_ADJUST_PRIORITY (0x3A0) = MVCP
 *                       (voice.dll replaces the handler; the server only sends after HELLO)
 *     MVCP = "MVC1" + u8 type + protobuf fields (MumbleProtocol.h codec).
 */

#ifndef MOD_VOICECHAT_VOICE_PROTOCOL_H
#define MOD_VOICECHAT_VOICE_PROTOCOL_H

#include "MumbleProtocol.h"

namespace VoiceProto
{
    constexpr uint16_t CMSG_OPCODE = 0x3AF;   // CMSG_VOICE_SESSION_ENABLE
    constexpr uint16_t SMSG_OPCODE = 0x3A0;   // SMSG_VOICE_SESSION_ADJUST_PRIORITY
    constexpr uint32_t VERSION = 1;
    constexpr uint8_t MAGIC[4] = { 'M', 'V', 'C', '1' };

    enum class Msg : uint8_t
    {
        Hello    = 1,   // C->S  version, capabilities
        Config   = 2,   // S->C  Murmur-Zugang + Nonce / Murmur access + nonce
        Context  = 3,   // S->C  Map/Instanz-Kontext / map/instance context
        Nearby   = 4,   // S->C  Mumble-Sessions in Hoerweite / sessions in range
        Bound    = 5,   // S->C  Binding bestaetigt / binding confirmed
        Disabled = 6,   // S->C  Voice fuer diesen Spieler aus / voice off for this player
    };

    struct Hello
    {
        uint32_t version = VERSION;
        uint32_t capabilities = 0;
    };

    struct Config
    {
        std::string host, password, certPin, nonce, username, botName, context;
        uint32_t port = 64738;
        float minDistance = 3.0f, maxDistance = 40.0f;
    };

    struct Context { std::string context; uint32_t seq = 0; };
    struct Nearby { std::vector<uint32_t> sessions; };
    struct Bound { uint32_t session = 0; };
    struct Disabled { std::string reason; };

    inline std::vector<uint8_t> Wrap(Msg type, const MumbleProto::Writer& w)
    {
        std::vector<uint8_t> out(MAGIC, MAGIC + 4);
        out.push_back(uint8_t(type));
        out.insert(out.end(), w.buf.begin(), w.buf.end());
        return out;
    }

    // DE: Findet MVCP in einem Puffer. EN: finds MVCP in a buffer.
    inline bool Unwrap(const uint8_t* data, size_t len, Msg& type, const uint8_t*& body, size_t& bodyLen)
    {
        if (len < 5 || std::memcmp(data, MAGIC, 4) != 0) return false;
        type = Msg(data[4]);
        body = data + 5;
        bodyLen = len - 5;
        return true;
    }

    // --- Kodieren / encode -------------------------------------------------------------------
    inline std::vector<uint8_t> Encode(const Hello& m)
    {
        MumbleProto::Writer w;
        w.U32(1, m.version);
        w.U32(2, m.capabilities);
        return Wrap(Msg::Hello, w);
    }

    inline std::vector<uint8_t> Encode(const Config& m)
    {
        MumbleProto::Writer w;
        w.Str(1, m.host);
        w.U32(2, m.port);
        w.Str(3, m.password);
        w.Str(4, m.certPin);
        w.Str(5, m.nonce);
        w.Str(6, m.username);
        w.Str(7, m.botName);
        w.Str(8, m.context);
        w.Float(9, m.minDistance);
        w.Float(10, m.maxDistance);
        return Wrap(Msg::Config, w);
    }

    inline std::vector<uint8_t> Encode(const Context& m)
    {
        MumbleProto::Writer w;
        w.Str(1, m.context);
        w.U32(2, m.seq);
        return Wrap(Msg::Context, w);
    }

    inline std::vector<uint8_t> Encode(const Nearby& m)
    {
        MumbleProto::Writer w;
        for (uint32_t s : m.sessions) w.U32(1, s);
        return Wrap(Msg::Nearby, w);
    }

    inline std::vector<uint8_t> Encode(const Bound& m)
    {
        MumbleProto::Writer w;
        w.U32(1, m.session);
        return Wrap(Msg::Bound, w);
    }

    inline std::vector<uint8_t> Encode(const Disabled& m)
    {
        MumbleProto::Writer w;
        w.Str(1, m.reason);
        return Wrap(Msg::Disabled, w);
    }

    // --- Dekodieren / decode -----------------------------------------------------------------
    inline bool Decode(const uint8_t* b, size_t n, Hello& m)
    {
        MumbleProto::Reader r(b, n);
        MumbleProto::Field f;
        while (r.Next(f))
        {
            if (f.number == 1) m.version = f.U32();
            else if (f.number == 2) m.capabilities = f.U32();
        }
        return r.ok();
    }

    inline bool Decode(const uint8_t* b, size_t n, Config& m)
    {
        MumbleProto::Reader r(b, n);
        MumbleProto::Field f;
        while (r.Next(f))
        {
            switch (f.number)
            {
                case 1: m.host = f.Str(); break;
                case 2: m.port = f.U32(); break;
                case 3: m.password = f.Str(); break;
                case 4: m.certPin = f.Str(); break;
                case 5: m.nonce = f.Str(); break;
                case 6: m.username = f.Str(); break;
                case 7: m.botName = f.Str(); break;
                case 8: m.context = f.Str(); break;
                case 9: m.minDistance = f.Float(); break;
                case 10: m.maxDistance = f.Float(); break;
                default: break;
            }
        }
        return r.ok();
    }

    inline bool Decode(const uint8_t* b, size_t n, Context& m)
    {
        MumbleProto::Reader r(b, n);
        MumbleProto::Field f;
        while (r.Next(f))
        {
            if (f.number == 1) m.context = f.Str();
            else if (f.number == 2) m.seq = f.U32();
        }
        return r.ok();
    }

    inline bool Decode(const uint8_t* b, size_t n, Nearby& m)
    {
        MumbleProto::Reader r(b, n);
        MumbleProto::Field f;
        while (r.Next(f))
            if (f.number == 1) m.sessions.push_back(f.U32());
        return r.ok();
    }

    inline bool Decode(const uint8_t* b, size_t n, Bound& m)
    {
        MumbleProto::Reader r(b, n);
        MumbleProto::Field f;
        while (r.Next(f))
            if (f.number == 1) m.session = f.U32();
        return r.ok();
    }

    inline bool Decode(const uint8_t* b, size_t n, Disabled& m)
    {
        MumbleProto::Reader r(b, n);
        MumbleProto::Field f;
        while (r.Next(f))
            if (f.number == 1) m.reason = f.Str();
        return r.ok();
    }

    // DE: Text, den der Client dem Bot per Mumble-TextMessage schickt. EN: text the client sends to the bot.
    inline std::string BindText(const std::string& nonce) { return "MVC1-BIND " + nonce; }
    inline bool ParseBindText(const std::string& text, std::string& nonce)
    {
        static const std::string prefix = "MVC1-BIND ";
        if (text.compare(0, prefix.size(), prefix) != 0) return false;
        nonce = text.substr(prefix.size());
        return nonce.size() >= 16 && nonce.size() <= 64;
    }
}

#endif // MOD_VOICECHAT_VOICE_PROTOCOL_H
