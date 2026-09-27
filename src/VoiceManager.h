/*
 * mod-voicechat - server logic: permissions, session binding, map/instance channels, proximity
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Ablauf pro Spieler:
 *     1. voice.dll schickt HELLO (MVCP) -> Rechte pruefen -> CONFIG mit Einmal-Nonce
 *     2. Client verbindet sich mit Murmur und schickt dem Bot "MVC1-BIND <nonce>"
 *     3. Bot meldet den Text -> Nonce pruefen -> Session <-> Charakter binden,
 *        in den Channel der Map/Instanz verschieben, BOUND an den Client
 *     4. Map-/Instanzwechsel -> CONTEXT an den Client + Bot verschiebt
 *     5. Jede Sekunde: Liste naher Sessions (NEARBY) -> Client fluestert nur diesen
 *     Nicht gebundene Mumble-Nutzer werden nach Voice.BindTimeoutSeconds gekickt.
 * EN: Flow per player:
 *     1. voice.dll sends HELLO (MVCP) -> check permissions -> CONFIG with one-time nonce
 *     2. client connects to Murmur and sends the bot "MVC1-BIND <nonce>"
 *     3. bot reports the text -> verify nonce -> bind session <-> character,
 *        move into the map/instance channel, BOUND to the client
 *     4. map/instance change -> CONTEXT to the client + bot moves
 *     5. every second: nearby strangers + group members (NEARBY) -> client whispers only to them;
 *        party/raid is always fully audible (position only for direction), even across maps
 *     Unbound Mumble users are kicked after Voice.BindTimeoutSeconds.
 */
#ifndef MOD_VOICECHAT_VOICE_MANAGER_H
#define MOD_VOICECHAT_VOICE_MANAGER_H

#include "MumbleBot.h"
#include "VoiceProtocol.h"

#include "ObjectGuid.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
class WorldPacket;
class WorldSession;

namespace VoiceChat
{
    struct Settings
    {
        bool enable = false;
        std::string publicHost = "127.0.0.1";
        uint32 publicPort = 64738;
        std::string serverPassword;
        std::string certSha256;
        MumbleBot::Config bot;
        std::string realm = "Realm";
        bool appendRealmToName = false;
        bool crossFaction = true;
        float minDistance = 3.0f;
        float maxDistance = 40.0f;
        uint32 minSecurity = 0;
        uint32 bindTimeoutSec = 30;
        uint32 nonceTtlSec = 60;
        uint32 nearbyIntervalMs = 1000;
        bool serverCulling = true;
        bool muteChatMuted = true;
        bool groupAlwaysAudible = true;
        std::vector<std::string> allowedExternalUsers;
    };

    class VoiceManager
    {
    public:
        static VoiceManager* instance();

        void LoadConfig(bool reload);
        void Start();
        void Stop();
        void Update(uint32 diff);

        // DE: true = Paket war MVCP und wurde verarbeitet. EN: true = packet was MVCP and has been handled.
        bool HandleClientPacket(WorldSession* session, WorldPacket const& packet);
        void OnLogout(Player* player);
        void OnMapChanged(Player* player);

        // DE: GM-Befehle. EN: GM commands.
        bool SetMute(Player* player, bool mute);
        bool Kick(Player* player);
        std::string Status();

    private:
        struct VoicePlayer
        {
            ObjectGuid guid;
            std::string username;
            std::string nonce;
            uint64 nonceExpire = 0;
            uint32 session = 0;              // gebundene Mumble-Session / bound Mumble session
            std::vector<std::string> path;
            std::string context;
            uint32 contextSeq = 0;
            std::vector<uint32> nearby;              // Fremde in Hoerweite / strangers in range
            std::vector<uint32> group;               // Gruppen-/Raidmitglieder / party/raid members
            bool listsDirty = true;                  // Listen neu senden / resend lists
            uint64 lastHello = 0;
            bool muted = false;              // GM-Mute / GM mute
            bool appliedMute = false;        // an Murmur gemeldeter Zustand / state sent to Murmur
            bool blocked = false;            // GM-Kick bis Relog / GM kick until relog
            bool dirty = false;              // Map/Instanz neu pruefen / re-check map/instance
        };

        void HandleHello(WorldSession* session, Player* player);
        void SendConfig(VoicePlayer& vp, Player* player);
        void HandleBotEvent(MumbleBot::Event const& e);
        void Bind(uint32 session, std::string const& nonce);
        void UpdateNearby();
        void Refresh(VoicePlayer& vp, Player* player, bool force);
        void ApplyMute(VoicePlayer& vp, Player* player);
        void Unbind(VoicePlayer& vp, std::string const& reason);
        VoicePlayer* Find(Player* player);
        void SendDisabled(Player* player, std::string const& reason);
        static void SendToClient(Player* player, std::vector<uint8_t> const& mvcp);

        std::vector<std::string> ChannelPath(Player* player) const;
        std::string Context(Player* player) const;
        bool IsAllowedExternal(std::string const& name) const;
        static uint64 NowMs();

        Settings _cfg;
        MumbleBot _bot;
        std::mutex _mutex;
        std::unordered_map<ObjectGuid::LowType, VoicePlayer> _players;
        std::unordered_map<uint32, ObjectGuid::LowType> _bySession;
        std::unordered_map<uint32, std::string> _mumbleNames;     // Session -> Mumble-Name
        std::unordered_map<uint32, uint64> _unbound;              // Session -> Beitrittszeit / join time
        uint32 _nearbyTimer = 0;
        bool _started = false;
    };
}

#define sVoiceManager VoiceChat::VoiceManager::instance()

#endif // MOD_VOICECHAT_VOICE_MANAGER_H
