/*
 * mod-voicechat - Mumble 1.5 client (see MumbleClient.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "voicecore/MumbleClient.h"
#include "voicecore/Net.h"
#include "voicecore/OcbCrypt.h"

#include <chrono>

using namespace MumbleProto;

namespace voicecore
{
    namespace
    {
        uint64_t NowMs()
        {
            return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        const char* OsName()
        {
#ifdef _WIN32
            return "Windows";
#else
            return "Linux";
#endif
        }

        constexpr uint64_t PING_INTERVAL_MS = 5000;
        constexpr uint64_t TCP_TIMEOUT_MS   = 30000;
        constexpr uint64_t UDP_TIMEOUT_MS   = 12000;
        constexpr size_t   MAX_AUDIO_QUEUE  = 50;
    }

    struct MumbleClient::NetState
    {
        TlsSocket tls;
        UdpSocket udp;
        OcbCrypt crypt;
        FrameParser parser;
        uint64_t lastTcpRecv = 0;
        uint64_t lastUdpRecv = 0;
        uint64_t lastPing = 0;
        bool fatal = false;
    };

    MumbleClient::~MumbleClient() { Stop(); }

    bool MumbleClient::Start(const ClientConfig& cfg)
    {
        Stop();
        _stop = false;
        _state = ClientState::Connecting;
        _session = 0;
        _udpActive = false;
        {
            std::lock_guard<std::mutex> g(_stateMutex);
            _users.clear();
            _channels.clear();
            _stats = Stats();
        }
        {
            std::lock_guard<std::mutex> g(_outMutex);
            _tcpOut.clear();
            _audioOut.clear();
        }
        _thread = std::thread(&MumbleClient::Run, this, cfg);
        return true;
    }

    void MumbleClient::Stop()
    {
        _stop = true;
        if (_thread.joinable())
            _thread.join();
        _state = ClientState::Disconnected;
    }

    void MumbleClient::SendTcp(Tcp type, std::vector<uint8_t> payload)
    {
        std::lock_guard<std::mutex> g(_outMutex);
        _tcpOut.push_back(Frame(type, payload));
    }

    void MumbleClient::SendAudio(const uint8_t* opus, size_t len, bool terminator, const float* pos3, uint32_t target)
    {
        if (_state != ClientState::Connected) return;
        UdpAudio a;
        a.targetOrContext = target;
        a.frameNumber = _frameNumber.fetch_add(2);     // 20 ms = 2 * 10-ms-Frames (Mumble-Zaehlung / counting)
        a.opus.assign(opus, opus + len);
        a.isTerminator = terminator;
        if (pos3)
        {
            a.hasPosition = true;
            for (int i = 0; i < 3; ++i) a.pos[i] = pos3[i];
        }
        std::vector<uint8_t> pkt = a.Encode(true);
        if (pkt.size() > MAX_UDP_PACKET_SIZE) return;
        std::lock_guard<std::mutex> g(_outMutex);
        if (_audioOut.size() >= MAX_AUDIO_QUEUE) _audioOut.pop_front();
        _audioOut.push_back(std::move(pkt));
    }

    void MumbleClient::SetSelfMute(bool mute, bool deaf)
    {
        MumbleProto::UserState us;
        us.session = _session.load();
        us.selfMute = mute || deaf;
        us.selfDeaf = deaf;
        SendTcp(Tcp::UserState, us.Encode());
    }

    void MumbleClient::SetPluginContext(const std::string& context, const std::string& identity)
    {
        MumbleProto::UserState us;
        us.session = _session.load();
        us.pluginContext = std::vector<uint8_t>(context.begin(), context.end());
        us.pluginIdentity = identity;
        SendTcp(Tcp::UserState, us.Encode());
    }

    std::vector<UserInfo> MumbleClient::Users() const
    {
        std::lock_guard<std::mutex> g(_stateMutex);
        std::vector<UserInfo> out;
        for (const auto& kv : _users) out.push_back(kv.second);
        return out;
    }

    std::string MumbleClient::ChannelName(uint32_t id) const
    {
        std::lock_guard<std::mutex> g(_stateMutex);
        auto it = _channels.find(id);
        return it == _channels.end() ? std::string() : it->second.first;
    }

    MumbleClient::Stats MumbleClient::GetStats() const
    {
        std::lock_guard<std::mutex> g(_stateMutex);
        return _stats;
    }

    // =======================================================================================
    //  Netzwerk-Thread / network thread
    // =======================================================================================
    void MumbleClient::Run(ClientConfig cfg)
    {
        NetState net;
        _net = &net;
        _disconnectReason.clear();

        std::string err;
        Log("connecting to " + cfg.host + ":" + std::to_string(cfg.port));
        if (!net.tls.Connect(cfg.host, cfg.port, cfg.certPinSha256, err))
        {
            _net = nullptr;
            _state = ClientState::Disconnected;
            if (onDisconnected) onDisconnected("TLS: " + err);
            return;
        }
        Log("TLS ok, server certificate sha256=" + net.tls.PeerFingerprint());
        _state = ClientState::Synchronizing;

        auto sendNow = [&](Tcp type, const std::vector<uint8_t>& p) {
            std::vector<uint8_t> f = Frame(type, p);
            if (!net.tls.WriteAll(f.data(), f.size())) net.fatal = true;
        };
        sendNow(Tcp::Version, EncodeVersion(cfg.release, OsName(), ""));
        sendNow(Tcp::Authenticate, EncodeAuthenticate(cfg.username, cfg.password, cfg.tokens, false));

        if (!cfg.forceTcp && !net.udp.Open(net.tls.RemoteIp(), cfg.port))
            Log("UDP socket failed, using TCP tunnel");

        net.lastTcpRecv = NowMs();
        std::vector<uint8_t> buf(16384);
        std::vector<uint8_t> plain(MAX_UDP_PACKET_SIZE + 64);

        while (!_stop && !net.fatal)
        {
            if (!net.tls.HasBuffered())
                WaitReadable(net.tls.Fd(), net.udp.Fd(), 5);

            // --- TCP lesen / read TCP ---
            for (;;)
            {
                int n = net.tls.Read(buf.data(), buf.size());
                if (n < 0) { net.fatal = true; _disconnectReason = "connection closed by server"; break; }
                if (n == 0) break;
                net.lastTcpRecv = NowMs();
                net.parser.Feed(buf.data(), size_t(n));
            }
            uint16_t type;
            std::vector<uint8_t> payload;
            while (net.parser.Pop(type, payload))
                HandleTcp(type, payload);
            if (net.parser.error()) { net.fatal = true; _disconnectReason = "protocol error"; }

            // --- UDP lesen / read UDP ---
            if (net.udp.Fd() >= 0)
            {
                for (;;)
                {
                    int n = net.udp.Recv(buf.data(), buf.size());
                    if (n <= 0) break;
                    if (size_t(n) <= OcbCrypt::OVERHEAD || size_t(n) > MAX_UDP_PACKET_SIZE + OcbCrypt::OVERHEAD) continue;
                    if (!net.crypt.Decrypt(buf.data(), plain.data(), size_t(n))) continue;
                    net.lastUdpRecv = NowMs();
                    HandleUdpPlain(plain.data(), size_t(n) - OcbCrypt::OVERHEAD, false);
                }
            }

            // --- Senden / send ---
            std::deque<std::vector<uint8_t>> tcpOut, audioOut;
            {
                std::lock_guard<std::mutex> g(_outMutex);
                tcpOut.swap(_tcpOut);
                audioOut.swap(_audioOut);
            }
            for (auto& f : tcpOut)
                if (!net.tls.WriteAll(f.data(), f.size())) { net.fatal = true; break; }

            bool useUdp = _udpActive && !cfg.forceTcp;
            for (auto& pkt : audioOut)
            {
                if (useUdp)
                {
                    std::vector<uint8_t> enc(pkt.size() + OcbCrypt::OVERHEAD);
                    if (net.crypt.Encrypt(pkt.data(), enc.data(), pkt.size()) && net.udp.Send(enc.data(), enc.size()))
                    {
                        std::lock_guard<std::mutex> g(_stateMutex);
                        ++_stats.udpSent;
                        continue;
                    }
                }
                std::vector<uint8_t> f = Frame(Tcp::UDPTunnel, pkt);
                if (!net.tls.WriteAll(f.data(), f.size())) { net.fatal = true; break; }
                std::lock_guard<std::mutex> g(_stateMutex);
                ++_stats.tcpTunnelSent;
            }

            // --- Pings und Timeouts / pings and timeouts ---
            uint64_t now = NowMs();
            if (now - net.lastPing >= PING_INTERVAL_MS && _state == ClientState::Connected)
            {
                net.lastPing = now;
                sendNow(Tcp::Ping, EncodePing(now));
                if (!cfg.forceTcp && net.crypt.IsValid() && net.udp.Fd() >= 0)
                {
                    std::vector<uint8_t> p = EncodeUdpPing(now);
                    std::vector<uint8_t> enc(p.size() + OcbCrypt::OVERHEAD);
                    if (net.crypt.Encrypt(p.data(), enc.data(), p.size()))
                        net.udp.Send(enc.data(), enc.size());
                }
            }
            if (_udpActive && now - net.lastUdpRecv > UDP_TIMEOUT_MS)
            {
                _udpActive = false;
                Log("UDP lost, falling back to TCP tunnel");
            }
            if (now - net.lastTcpRecv > TCP_TIMEOUT_MS)
            {
                net.fatal = true;
                _disconnectReason = "timeout";
            }

            {
                std::lock_guard<std::mutex> g(_stateMutex);
                _stats.cryptGood = net.crypt.good;
                _stats.cryptLost = net.crypt.lost;
                _stats.cryptLate = net.crypt.late;
            }
        }

        net.udp.Close();
        net.tls.Close();
        _net = nullptr;
        _state = ClientState::Disconnected;
        _udpActive = false;
        if (onDisconnected) onDisconnected(_stop ? std::string("stopped") : _disconnectReason);
    }

    void MumbleClient::HandleTcp(uint16_t type, const std::vector<uint8_t>& p)
    {
        NetState& net = *_net;
        switch (Tcp(type))
        {
            case Tcp::UDPTunnel:
            {
                {
                    std::lock_guard<std::mutex> g(_stateMutex);
                    ++_stats.tcpTunnelRecv;
                }
                HandleUdpPlain(p.data(), p.size(), true);
                break;
            }
            case Tcp::Reject:
            {
                Reject r;
                r.Decode(p);
                _disconnectReason = "rejected (" + std::to_string(r.type) + "): " + r.reason;
                net.fatal = true;
                break;
            }
            case Tcp::ServerSync:
            {
                ServerSync s;
                s.Decode(p);
                _session = s.session;
                _state = ClientState::Connected;
                Log("connected, session " + std::to_string(s.session));
                if (onConnected) onConnected(s.session);
                break;
            }
            case Tcp::CryptSetup:
            {
                CryptSetup c;
                c.Decode(p);
                if (!c.key.empty() && !c.clientNonce.empty() && !c.serverNonce.empty())
                {
                    // DE: client_nonce = unser Encrypt-IV, server_nonce = unser Decrypt-IV
                    // EN: client_nonce = our encrypt IV, server_nonce = our decrypt IV
                    if (!net.crypt.SetKey(c.key, c.clientNonce, c.serverNonce))
                        Log("invalid CryptSetup");
                    net.lastPing = 0;   // sofort UDP-Ping / UDP ping right away
                }
                else if (!c.serverNonce.empty())
                    net.crypt.SetDecryptIv(c.serverNonce);
                else
                {
                    CryptSetup reply;
                    reply.clientNonce = net.crypt.EncryptIv();
                    SendTcp(Tcp::CryptSetup, reply.Encode());
                }
                break;
            }
            case Tcp::ChannelState:
            {
                MumbleProto::ChannelState c;
                if (!c.Decode(p) || !c.channelId) break;
                std::lock_guard<std::mutex> g(_stateMutex);
                auto& e = _channels[*c.channelId];
                if (c.name) e.first = *c.name;
                if (c.parent) e.second = *c.parent;
                break;
            }
            case Tcp::ChannelRemove:
            {
                if (auto id = DecodeSessionField(p, 1))
                {
                    std::lock_guard<std::mutex> g(_stateMutex);
                    _channels.erase(*id);
                }
                break;
            }
            case Tcp::UserState:
            {
                MumbleProto::UserState u;
                if (!u.Decode(p) || !u.session) break;
                std::lock_guard<std::mutex> g(_stateMutex);
                UserInfo& e = _users[*u.session];
                e.session = *u.session;
                if (u.name) e.name = *u.name;
                if (u.channelId) e.channel = *u.channelId;
                if (u.mute) e.mute = *u.mute;
                if (u.deaf) e.deaf = *u.deaf;
                if (u.suppress) e.suppress = *u.suppress;
                if (u.selfMute) e.selfMute = *u.selfMute;
                if (u.selfDeaf) e.selfDeaf = *u.selfDeaf;
                break;
            }
            case Tcp::UserRemove:
            {
                auto s = DecodeSessionField(p, 1);
                if (!s) break;
                if (*s == _session)
                {
                    _disconnectReason = "removed from server (kick/ban)";
                    net.fatal = true;
                }
                std::lock_guard<std::mutex> g(_stateMutex);
                _users.erase(*s);
                break;
            }
            case Tcp::TextMessage:
            {
                MumbleProto::TextMessage t;
                if (t.Decode(p) && onTextMessage) onTextMessage(t);
                break;
            }
            case Tcp::PermissionDenied:
                Log("permission denied");
                break;
            default:
                break;   // Version, Ping, CodecVersion, ServerConfig, ... ignorieren / ignore
        }
    }

    void MumbleClient::HandleUdpPlain(const uint8_t* data, size_t len, bool viaTunnel)
    {
        if (len < 1) return;
        if (data[0] == uint8_t(Udp::Ping))
        {
            if (!viaTunnel && !_udpActive)
            {
                _udpActive = true;
                Log("UDP active");
            }
            return;
        }
        if (!viaTunnel)
        {
            std::lock_guard<std::mutex> g(_stateMutex);
            ++_stats.udpRecv;
        }
        UdpAudio a;
        if (a.Decode(data, len) && onAudio)
            onAudio(a);
    }
}
