/*
 * mod-voicechat - Mumble control bot (Boost.Asio + OpenSSL, no AzerothCore dependency)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Verbindet sich als privilegierter Nutzer (SuperUser) mit Murmur, legt die
 *     Channel-Struktur an, setzt die ACLs und verschiebt/mutet/kickt Spieler.
 *     Eigener Thread (io_context). Befehle sind threadsicher, Ereignisse werden
 *     per PollEvents() abgeholt (z. B. im World-Update von AzerothCore).
 * EN: Connects to Murmur as a privileged user (SuperUser), creates the channel
 *     tree, sets the ACLs and moves/mutes/kicks players. Own thread
 *     (io_context). Commands are thread-safe, events are fetched via
 *     PollEvents() (e.g. in AzerothCore's world update).
 */
#ifndef MOD_VOICECHAT_MUMBLE_BOT_H
#define MOD_VOICECHAT_MUMBLE_BOT_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace VoiceChat
{
    class MumbleBot
    {
    public:
        struct Config
        {
            std::string host = "127.0.0.1";
            uint16_t port = 64738;
            std::string username = "SuperUser";
            std::string password;
            std::string certPinSha256;          // optional
            std::string rootChannel = "WoW";    // unter Root / below root
            std::string lobbyChannel = "Lobby"; // Aufenthaltsort des Bots / where the bot sits
            uint32_t emptyChannelTimeoutSec = 120;
            bool verbose = false;               // ausfuehrliches Log / verbose log
            // DE: Murmur begrenzt ChannelState/ACL/TextMessage (messagelimit/messageburst). Etwas darunter bleiben.
            // EN: Murmur rate-limits ChannelState/ACL/TextMessage (messagelimit/messageburst). Stay slightly below.
            double messageRate = 0.9;           // pro Sekunde / per second
            double messageBurst = 4.0;
        };

        struct Event
        {
            enum Type { Connected, Disconnected, UserJoined, UserLeft, Text };
            Type type = Connected;
            uint32_t session = 0;
            std::string name;   // UserJoined
            std::string text;   // Text, Disconnected (Grund / reason)
        };

        MumbleBot();
        ~MumbleBot();

        void Start(const Config& cfg);
        void Stop();
        bool IsConnected() const;
        uint32_t OwnSession() const;

        // DE: threadsicher. Pfad relativ zum Root-Channel (z. B. {"Realm","Map-533","Inst-7"}).
        // EN: thread-safe. Path relative to the root channel (e.g. {"Realm","Map-533","Inst-7"}).
        void MoveUser(uint32_t session, const std::vector<std::string>& path);
        void Kick(uint32_t session, const std::string& reason);
        void SetMute(uint32_t session, bool mute);
        void SendText(uint32_t session, const std::string& text);

        std::vector<Event> PollEvents();
        std::function<void(const std::string&)> onLog;

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}

#endif // MOD_VOICECHAT_MUMBLE_BOT_H
