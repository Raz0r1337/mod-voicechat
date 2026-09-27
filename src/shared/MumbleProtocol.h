/*
 * mod-voicechat - Minimal Mumble protocol codec (header-only, no dependencies)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Gemeinsamer Code fuer das AzerothCore-Modul (Bot) und den Client
 *     (voice.dll). Enthaelt einen minimalen Protobuf-Codec, die TCP-Rahmung
 *     und genau die Mumble-Nachrichten, die wir brauchen. Feldnummern laut
 *     Mumble.proto / MumbleUDP.proto (Mumble 1.5, BSD-3-Clause).
 * EN: Shared code for the AzerothCore module (bot) and the client
 *     (voice.dll). Contains a minimal protobuf codec, TCP framing and exactly
 *     the Mumble messages we need. Field numbers as in Mumble.proto /
 *     MumbleUDP.proto (Mumble 1.5, BSD-3-Clause).
 */

#ifndef MOD_VOICECHAT_MUMBLE_PROTOCOL_H
#define MOD_VOICECHAT_MUMBLE_PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace MumbleProto
{
    // --- Versionen / versions -------------------------------------------------------------
    constexpr uint32_t VERSION_V1 = (1u << 16) | (5u << 8) | 0u;                    // 1.5.0 (legacy)
    constexpr uint64_t VERSION_V2 = (uint64_t(1) << 48) | (uint64_t(5) << 32);      // 1.5.0

    // --- TCP-Nachrichtentypen / TCP message types (MumbleProtocol.h) ------------------------
    enum class Tcp : uint16_t
    {
        Version = 0, UDPTunnel = 1, Authenticate = 2, Ping = 3, Reject = 4, ServerSync = 5,
        ChannelRemove = 6, ChannelState = 7, UserRemove = 8, UserState = 9, BanList = 10,
        TextMessage = 11, PermissionDenied = 12, ACL = 13, QueryUsers = 14, CryptSetup = 15,
        ContextActionModify = 16, ContextAction = 17, UserList = 18, VoiceTarget = 19,
        PermissionQuery = 20, CodecVersion = 21, UserStats = 22, RequestBlob = 23,
        ServerConfig = 24, SuggestConfig = 25, PluginDataTransmission = 26
    };

    // --- UDP-Nachrichtentypen (1.5-Protobuf-Format, erstes Byte) / UDP message types -------
    enum class Udp : uint8_t { Audio = 0, Ping = 1 };

    constexpr size_t TCP_HEADER_SIZE     = 6;       // u16 type (BE) + u32 length (BE)
    constexpr size_t MAX_TCP_PAYLOAD     = 8 * 1024 * 1024;
    constexpr size_t MAX_UDP_PACKET_SIZE = 1024;
    constexpr uint32_t TARGET_NORMAL     = 0;
    constexpr uint32_t TARGET_LOOPBACK   = 31;

    // =======================================================================================
    //  Protobuf-Wire-Format / protobuf wire format
    // =======================================================================================
    enum WireType : uint8_t { VARINT = 0, FIXED64 = 1, LENGTH = 2, FIXED32 = 5 };

    class Writer
    {
    public:
        std::vector<uint8_t> buf;

        void Varint(uint64_t v)
        {
            while (v >= 0x80) { buf.push_back(uint8_t(v) | 0x80); v >>= 7; }
            buf.push_back(uint8_t(v));
        }
        void Key(uint32_t field, WireType wt) { Varint((uint64_t(field) << 3) | wt); }
        void U64(uint32_t f, uint64_t v) { Key(f, VARINT); Varint(v); }
        void U32(uint32_t f, uint32_t v) { U64(f, v); }
        void I32(uint32_t f, int32_t v) { U64(f, uint64_t(int64_t(v))); }
        void Bool(uint32_t f, bool v) { U64(f, v ? 1 : 0); }
        void Float(uint32_t f, float v)
        {
            Key(f, FIXED32);
            uint32_t u; std::memcpy(&u, &v, 4);
            for (int i = 0; i < 4; ++i) buf.push_back(uint8_t(u >> (8 * i)));
        }
        void Bytes(uint32_t f, const void* data, size_t len)
        {
            Key(f, LENGTH); Varint(len);
            const uint8_t* p = static_cast<const uint8_t*>(data);
            buf.insert(buf.end(), p, p + len);
        }
        void Str(uint32_t f, const std::string& s) { Bytes(f, s.data(), s.size()); }
        void Msg(uint32_t f, const Writer& w) { Bytes(f, w.buf.data(), w.buf.size()); }
    };

    struct Field
    {
        uint32_t number = 0;
        WireType type = VARINT;
        uint64_t value = 0;               // VARINT/FIXED32/FIXED64
        const uint8_t* data = nullptr;    // LENGTH
        size_t size = 0;

        uint32_t U32() const { return uint32_t(value); }
        bool Bool() const { return value != 0; }
        float Float() const { uint32_t u = uint32_t(value); float f; std::memcpy(&f, &u, 4); return f; }
        std::string Str() const { return std::string(reinterpret_cast<const char*>(data), size); }
    };

    class Reader
    {
    public:
        Reader(const uint8_t* data, size_t size) : _p(data), _end(data + size) { }

        // DE: liefert false am Ende oder bei kaputten Daten (dann ok() == false)
        // EN: returns false at the end or on malformed data (then ok() == false)
        bool Next(Field& f)
        {
            if (_p >= _end || !_ok) return false;
            // DE: Keine Reste des vorigen Felds (falscher Wire-Typ -> leer statt alter Daten).
            // EN: no leftovers from the previous field (wrong wire type -> empty instead of old data).
            f.value = 0; f.data = nullptr; f.size = 0;
            uint64_t key;
            if (!ReadVarint(key)) return Fail();
            f.number = uint32_t(key >> 3);
            f.type = WireType(key & 7);
            switch (f.type)
            {
                case VARINT: return ReadVarint(f.value) || Fail();
                case FIXED64:
                    if (_end - _p < 8) return Fail();
                    f.value = 0; for (int i = 0; i < 8; ++i) f.value |= uint64_t(_p[i]) << (8 * i);
                    _p += 8; return true;
                case FIXED32:
                    if (_end - _p < 4) return Fail();
                    f.value = 0; for (int i = 0; i < 4; ++i) f.value |= uint64_t(_p[i]) << (8 * i);
                    _p += 4; return true;
                case LENGTH:
                {
                    uint64_t len;
                    if (!ReadVarint(len) || len > uint64_t(_end - _p)) return Fail();
                    f.data = _p; f.size = size_t(len); _p += len; return true;
                }
                default: return Fail();
            }
        }
        bool ok() const { return _ok; }

    private:
        bool ReadVarint(uint64_t& v)
        {
            v = 0;
            for (int shift = 0; shift < 64 && _p < _end; shift += 7)
            {
                uint8_t b = *_p++;
                v |= uint64_t(b & 0x7F) << shift;
                if (!(b & 0x80)) return true;
            }
            return false;
        }
        bool Fail() { _ok = false; return false; }

        const uint8_t* _p;
        const uint8_t* _end;
        bool _ok = true;
    };

    // =======================================================================================
    //  TCP-Rahmung / TCP framing
    // =======================================================================================
    inline std::vector<uint8_t> Frame(Tcp type, const std::vector<uint8_t>& payload)
    {
        std::vector<uint8_t> out(TCP_HEADER_SIZE + payload.size());
        uint16_t t = uint16_t(type);
        uint32_t l = uint32_t(payload.size());
        out[0] = uint8_t(t >> 8); out[1] = uint8_t(t);
        out[2] = uint8_t(l >> 24); out[3] = uint8_t(l >> 16); out[4] = uint8_t(l >> 8); out[5] = uint8_t(l);
        if (!payload.empty()) std::memcpy(out.data() + TCP_HEADER_SIZE, payload.data(), payload.size());
        return out;
    }

    // DE: Zerlegt einen Stream in Nachrichten. EN: Splits a stream into messages.
    class FrameParser
    {
    public:
        void Feed(const uint8_t* data, size_t len) { _buf.insert(_buf.end(), data, data + len); }

        // false = keine vollstaendige Nachricht / no complete message; error() bei Protokollfehler
        bool Pop(uint16_t& type, std::vector<uint8_t>& payload)
        {
            if (_buf.size() < TCP_HEADER_SIZE) return false;
            type = uint16_t((_buf[0] << 8) | _buf[1]);
            uint32_t len = (uint32_t(_buf[2]) << 24) | (uint32_t(_buf[3]) << 16) | (uint32_t(_buf[4]) << 8) | _buf[5];
            if (len > MAX_TCP_PAYLOAD) { _error = true; return false; }
            if (_buf.size() < TCP_HEADER_SIZE + len) return false;
            payload.assign(_buf.begin() + TCP_HEADER_SIZE, _buf.begin() + TCP_HEADER_SIZE + len);
            _buf.erase(_buf.begin(), _buf.begin() + TCP_HEADER_SIZE + len);
            return true;
        }
        bool error() const { return _error; }

    private:
        std::vector<uint8_t> _buf;
        bool _error = false;
    };

    // =======================================================================================
    //  Nachrichten / messages
    // =======================================================================================
    inline std::vector<uint8_t> EncodeVersion(const std::string& release, const std::string& os, const std::string& osVersion)
    {
        Writer w;
        w.U32(1, VERSION_V1);
        w.Str(2, release);
        w.Str(3, os);
        w.Str(4, osVersion);
        w.U64(5, VERSION_V2);
        return w.buf;
    }

    inline std::vector<uint8_t> EncodeAuthenticate(const std::string& user, const std::string& password,
                                                   const std::vector<std::string>& tokens, bool bot)
    {
        Writer w;
        w.Str(1, user);
        if (!password.empty()) w.Str(2, password);
        for (const auto& t : tokens) w.Str(3, t);
        w.Bool(5, true);                  // opus
        w.I32(6, bot ? 1 : 0);            // client_type
        return w.buf;
    }

    inline std::vector<uint8_t> EncodePing(uint64_t timestamp)
    {
        Writer w;
        w.U64(1, timestamp);
        return w.buf;
    }

    struct UserState
    {
        std::optional<uint32_t> session, actor, userId, channelId;
        std::optional<std::string> name, pluginIdentity, hash, comment;
        std::optional<std::vector<uint8_t>> pluginContext;
        std::optional<bool> mute, deaf, suppress, selfMute, selfDeaf;

        std::vector<uint8_t> Encode() const
        {
            Writer w;
            if (session) w.U32(1, *session);
            if (actor) w.U32(2, *actor);
            if (name) w.Str(3, *name);
            if (userId) w.U32(4, *userId);
            if (channelId) w.U32(5, *channelId);
            if (mute) w.Bool(6, *mute);
            if (deaf) w.Bool(7, *deaf);
            if (suppress) w.Bool(8, *suppress);
            if (selfMute) w.Bool(9, *selfMute);
            if (selfDeaf) w.Bool(10, *selfDeaf);
            if (pluginContext) w.Bytes(12, pluginContext->data(), pluginContext->size());
            if (pluginIdentity) w.Str(13, *pluginIdentity);
            if (comment) w.Str(14, *comment);
            return w.buf;
        }

        bool Decode(const std::vector<uint8_t>& d)
        {
            Reader r(d.data(), d.size());
            Field f;
            while (r.Next(f))
            {
                switch (f.number)
                {
                    case 1: session = f.U32(); break;
                    case 2: actor = f.U32(); break;
                    case 3: name = f.Str(); break;
                    case 4: userId = f.U32(); break;
                    case 5: channelId = f.U32(); break;
                    case 6: mute = f.Bool(); break;
                    case 7: deaf = f.Bool(); break;
                    case 8: suppress = f.Bool(); break;
                    case 9: selfMute = f.Bool(); break;
                    case 10: selfDeaf = f.Bool(); break;
                    case 12: pluginContext = std::vector<uint8_t>(f.data, f.data + f.size); break;
                    case 13: pluginIdentity = f.Str(); break;
                    case 14: comment = f.Str(); break;
                    case 15: hash = f.Str(); break;
                    default: break;
                }
            }
            return r.ok();
        }
    };

    struct ChannelState
    {
        std::optional<uint32_t> channelId, parent, maxUsers;
        std::optional<std::string> name, description;
        std::optional<bool> temporary;

        std::vector<uint8_t> Encode() const
        {
            Writer w;
            if (channelId) w.U32(1, *channelId);
            if (parent) w.U32(2, *parent);
            if (name) w.Str(3, *name);
            if (description) w.Str(5, *description);
            if (temporary) w.Bool(8, *temporary);
            if (maxUsers) w.U32(11, *maxUsers);
            return w.buf;
        }

        bool Decode(const std::vector<uint8_t>& d)
        {
            Reader r(d.data(), d.size());
            Field f;
            while (r.Next(f))
            {
                switch (f.number)
                {
                    case 1: channelId = f.U32(); break;
                    case 2: parent = f.U32(); break;
                    case 3: name = f.Str(); break;
                    case 5: description = f.Str(); break;
                    case 8: temporary = f.Bool(); break;
                    case 11: maxUsers = f.U32(); break;
                    default: break;
                }
            }
            return r.ok();
        }
    };

    // DE: Einfache Nachrichten mit nur wenigen Feldern. EN: Simple messages with few fields.
    inline std::optional<uint32_t> DecodeSessionField(const std::vector<uint8_t>& d, uint32_t fieldNo)
    {
        Reader r(d.data(), d.size());
        Field f;
        while (r.Next(f))
            if (f.number == fieldNo && f.type == VARINT) return f.U32();
        return std::nullopt;
    }

    struct ServerSync
    {
        uint32_t session = 0;
        uint32_t maxBandwidth = 0;
        std::string welcomeText;

        bool Decode(const std::vector<uint8_t>& d)
        {
            Reader r(d.data(), d.size());
            Field f;
            while (r.Next(f))
            {
                if (f.number == 1) session = f.U32();
                else if (f.number == 2) maxBandwidth = f.U32();
                else if (f.number == 3) welcomeText = f.Str();
            }
            return r.ok();
        }
    };

    struct Reject
    {
        uint32_t type = 0;
        std::string reason;

        bool Decode(const std::vector<uint8_t>& d)
        {
            Reader r(d.data(), d.size());
            Field f;
            while (r.Next(f))
            {
                if (f.number == 1) type = f.U32();
                else if (f.number == 2) reason = f.Str();
            }
            return r.ok();
        }
    };

    struct CryptSetup
    {
        std::string key, clientNonce, serverNonce;

        std::vector<uint8_t> Encode() const
        {
            Writer w;
            if (!key.empty()) w.Str(1, key);
            if (!clientNonce.empty()) w.Str(2, clientNonce);
            if (!serverNonce.empty()) w.Str(3, serverNonce);
            return w.buf;
        }

        bool Decode(const std::vector<uint8_t>& d)
        {
            Reader r(d.data(), d.size());
            Field f;
            while (r.Next(f))
            {
                if (f.number == 1) key = f.Str();
                else if (f.number == 2) clientNonce = f.Str();
                else if (f.number == 3) serverNonce = f.Str();
            }
            return r.ok();
        }
    };

    struct TextMessage
    {
        std::optional<uint32_t> actor;
        std::vector<uint32_t> sessions, channels, trees;
        std::string message;

        std::vector<uint8_t> Encode() const
        {
            Writer w;
            if (actor) w.U32(1, *actor);
            for (uint32_t s : sessions) w.U32(2, s);
            for (uint32_t c : channels) w.U32(3, c);
            for (uint32_t t : trees) w.U32(4, t);
            w.Str(5, message);
            return w.buf;
        }

        bool Decode(const std::vector<uint8_t>& d)
        {
            Reader r(d.data(), d.size());
            Field f;
            while (r.Next(f))
            {
                switch (f.number)
                {
                    case 1: actor = f.U32(); break;
                    case 2: sessions.push_back(f.U32()); break;
                    case 3: channels.push_back(f.U32()); break;
                    case 4: trees.push_back(f.U32()); break;
                    case 5: message = f.Str(); break;
                    default: break;
                }
            }
            return r.ok();
        }
    };

    // DE: VoiceTarget (Whisper/Shout) - fuer Proximity-Culling (Phase 4)
    // EN: VoiceTarget (whisper/shout) - for proximity culling (phase 4)
    inline std::vector<uint8_t> EncodeVoiceTargetSessions(uint32_t id, const std::vector<uint32_t>& sessions)
    {
        Writer target;
        for (uint32_t s : sessions) target.U32(1, s);
        Writer w;
        w.U32(1, id);
        w.Msg(2, target);
        return w.buf;
    }

    // =======================================================================================
    //  UDP (Mumble >= 1.5: 1 Byte Typ + Protobuf) / UDP (type byte + protobuf)
    // =======================================================================================
    struct UdpAudio
    {
        uint32_t targetOrContext = 0;           // C->S: target, S->C: context
        uint32_t senderSession = 0;             // nur S->C / S->C only
        uint64_t frameNumber = 0;
        std::vector<uint8_t> opus;
        std::optional<float> pos[3];
        float volumeAdjustment = 0.0f;
        bool isTerminator = false;
        bool hasPosition = false;

        // DE: fromClient=true -> Feld 1 (target), sonst Feld 2 (context)
        // EN: fromClient=true -> field 1 (target), otherwise field 2 (context)
        std::vector<uint8_t> Encode(bool fromClient) const
        {
            Writer w;
            w.buf.push_back(uint8_t(Udp::Audio));
            w.U32(fromClient ? 1 : 2, targetOrContext);
            if (!fromClient) w.U32(3, senderSession);
            w.U64(4, frameNumber);
            w.Bytes(5, opus.data(), opus.size());
            if (hasPosition)
            {
                // DE: repeated float, als packed codiert (proto3-Standard)
                // EN: repeated float, encoded packed (proto3 default)
                Writer p;
                for (int i = 0; i < 3; ++i) { float v = pos[i].value_or(0.0f); uint32_t u; std::memcpy(&u, &v, 4); for (int b = 0; b < 4; ++b) p.buf.push_back(uint8_t(u >> (8 * b))); }
                w.Bytes(6, p.buf.data(), p.buf.size());
            }
            if (isTerminator) w.Bool(16, true);
            return w.buf;
        }

        bool Decode(const uint8_t* data, size_t len)
        {
            if (len < 1 || data[0] != uint8_t(Udp::Audio)) return false;
            Reader r(data + 1, len - 1);
            Field f;
            int posCount = 0;
            while (r.Next(f))
            {
                switch (f.number)
                {
                    case 1: case 2: targetOrContext = f.U32(); break;
                    case 3: senderSession = f.U32(); break;
                    case 4: frameNumber = f.value; break;
                    case 5: opus.assign(f.data, f.data + f.size); break;
                    case 6:
                        if (f.type == LENGTH)       // packed
                        {
                            for (size_t i = 0; i + 4 <= f.size && posCount < 3; i += 4)
                            {
                                uint32_t u = uint32_t(f.data[i]) | (uint32_t(f.data[i + 1]) << 8) | (uint32_t(f.data[i + 2]) << 16) | (uint32_t(f.data[i + 3]) << 24);
                                float v; std::memcpy(&v, &u, 4); pos[posCount++] = v;
                            }
                        }
                        else if (f.type == FIXED32 && posCount < 3)
                            pos[posCount++] = f.Float();
                        break;
                    case 7: volumeAdjustment = f.Float(); break;
                    case 16: isTerminator = f.Bool(); break;
                    default: break;
                }
            }
            hasPosition = posCount == 3;
            return r.ok();
        }
    };

    inline std::vector<uint8_t> EncodeUdpPing(uint64_t timestamp)
    {
        Writer w;
        w.buf.push_back(uint8_t(Udp::Ping));
        w.U64(1, timestamp);
        return w.buf;
    }

    inline bool DecodeUdpPing(const uint8_t* data, size_t len, uint64_t& timestamp)
    {
        if (len < 1 || data[0] != uint8_t(Udp::Ping)) return false;
        Reader r(data + 1, len - 1);
        Field f;
        timestamp = 0;
        while (r.Next(f))
            if (f.number == 1) timestamp = f.value;
        return r.ok();
    }
}

#endif // MOD_VOICECHAT_MUMBLE_PROTOCOL_H
