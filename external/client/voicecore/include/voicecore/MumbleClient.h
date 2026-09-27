/*
 * mod-voicechat - Mumble 1.5 client (control + voice transport)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Ein eigener Netzwerk-Thread pro Verbindung. Alle Callbacks laufen in
 *     diesem Thread - sie duerfen NICHT blockieren und keine WoW-Funktionen
 *     aufrufen. Reconnect ist Aufgabe des Besitzers (Start/Stop).
 * EN: One network thread per connection. All callbacks run in that thread -
 *     they must NOT block and must not call WoW functions. Reconnecting is
 *     the owner's job (Start/Stop).
 */
#pragma once

#include "MumbleProtocol.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace voicecore
{
    struct ClientConfig
    {
        std::string host = "127.0.0.1";
        uint16_t port = 64738;
        std::string username;
        std::string password;                 // Server-Passwort / server password
        std::vector<std::string> tokens;      // Access-Tokens
        std::string certPinSha256;            // optional
        bool forceTcp = false;                // nur UDP-Tunnel ueber TCP / only UDP tunnel over TCP
        std::string release = "mod-voicechat 0.1";
    };

    struct UserInfo
    {
        uint32_t session = 0;
        std::string name;
        uint32_t channel = 0;
        bool mute = false, deaf = false, suppress = false, selfMute = false, selfDeaf = false;
    };

    enum class ClientState { Disconnected, Connecting, Synchronizing, Connected };

    class MumbleClient
    {
    public:
        MumbleClient() = default;
        ~MumbleClient();
        MumbleClient(const MumbleClient&) = delete;
        MumbleClient& operator=(const MumbleClient&) = delete;

        std::function<void(const std::string&)> onLog;
        std::function<void(uint32_t session)> onConnected;
        std::function<void(const std::string& reason)> onDisconnected;
        std::function<void(const MumbleProto::UdpAudio&)> onAudio;
        std::function<void(const MumbleProto::TextMessage&)> onTextMessage;

        bool Start(const ClientConfig& cfg);
        void Stop();

        ClientState State() const { return _state; }
        uint32_t Session() const { return _session; }
        bool UdpActive() const { return _udpActive; }

        // DE: threadsicher. EN: thread-safe.
        void SendAudio(const uint8_t* opus, size_t len, bool terminator, const float* pos3, uint32_t target = MumbleProto::TARGET_NORMAL);
        void SendTcp(MumbleProto::Tcp type, std::vector<uint8_t> payload);
        void SetSelfMute(bool mute, bool deaf);
        void SetPluginContext(const std::string& context, const std::string& identity);
        std::vector<UserInfo> Users() const;
        std::string ChannelName(uint32_t id) const;

        struct Stats { uint32_t udpSent = 0, tcpTunnelSent = 0, udpRecv = 0, tcpTunnelRecv = 0, cryptGood = 0, cryptLost = 0, cryptLate = 0; };
        Stats GetStats() const;

    private:
        void Run(ClientConfig cfg);
        void HandleTcp(uint16_t type, const std::vector<uint8_t>& payload);
        void HandleUdpPlain(const uint8_t* data, size_t len, bool viaTunnel);
        void Log(const std::string& msg) { if (onLog) onLog(msg); }

        std::thread _thread;
        std::atomic<bool> _stop{ false };
        std::atomic<ClientState> _state{ ClientState::Disconnected };
        std::atomic<uint32_t> _session{ 0 };
        std::atomic<bool> _udpActive{ false };
        std::atomic<uint64_t> _frameNumber{ 0 };

        mutable std::mutex _outMutex;
        std::deque<std::vector<uint8_t>> _tcpOut;       // fertige Frames / framed messages
        std::deque<std::vector<uint8_t>> _audioOut;     // UDP-Klartext / UDP plaintext

        mutable std::mutex _stateMutex;
        std::map<uint32_t, UserInfo> _users;
        std::map<uint32_t, std::pair<std::string, uint32_t>> _channels;   // id -> (name, parent)
        Stats _stats;

        // DE: nur im Netzwerk-Thread benutzt. EN: used only in the network thread.
        struct NetState;
        NetState* _net = nullptr;
        std::string _disconnectReason;
    };
}
