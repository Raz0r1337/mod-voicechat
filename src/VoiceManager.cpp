/*
 * mod-voicechat - server logic: permissions, session binding, map/instance channels, proximity
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Laeuft komplett im World-Thread (OnWorldUpdate kommt nach MapMgr::Update,
 *     CMSG_VOICE_SESSION_ENABLE ist PROCESS_THREADUNSAFE). Der Mutex schuetzt nur
 *     gegen Hooks, die AzerothCore evtl. aus Map-Threads aufruft.
 * EN: Runs entirely in the world thread (OnWorldUpdate comes after MapMgr::Update,
 *     CMSG_VOICE_SESSION_ENABLE is PROCESS_THREADUNSAFE). The mutex only guards
 *     against hooks AzerothCore may call from map threads.
 */
#include "VoiceManager.h"

#include "Config.h"
#include "CryptoRandom.h"
#include "Group.h"
#include "GroupReference.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "Util.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <map>
#include <sstream>

namespace VoiceChat
{
    VoiceManager* VoiceManager::instance()
    {
        static VoiceManager inst;
        return &inst;
    }

    uint64 VoiceManager::NowMs()
    {
        return uint64(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // ------------------------------------------------------------------------------------------
    //  Konfiguration / configuration
    // ------------------------------------------------------------------------------------------
    void VoiceManager::LoadConfig(bool reload)
    {
        Settings s;
        s.enable            = sConfigMgr->GetOption<bool>("Voice.Enable", false);
        s.publicHost        = sConfigMgr->GetOption<std::string>("Voice.PublicHost", "127.0.0.1");
        s.publicPort        = sConfigMgr->GetOption<uint32>("Voice.PublicPort", 64738);
        s.serverPassword    = sConfigMgr->GetOption<std::string>("Voice.ServerPassword", "");
        s.certSha256        = sConfigMgr->GetOption<std::string>("Voice.CertSha256", "");
        s.realm             = sConfigMgr->GetOption<std::string>("Voice.RealmName", "Realm");
        s.appendRealmToName = sConfigMgr->GetOption<bool>("Voice.AppendRealmToName", false);
        s.crossFaction      = sConfigMgr->GetOption<bool>("Voice.CrossFaction", true);
        s.minDistance       = sConfigMgr->GetOption<float>("Voice.MinDistance", 3.0f);
        s.maxDistance       = sConfigMgr->GetOption<float>("Voice.MaxDistance", 40.0f);
        s.minSecurity       = sConfigMgr->GetOption<uint32>("Voice.MinSecurity", 0);
        s.bindTimeoutSec    = sConfigMgr->GetOption<uint32>("Voice.BindTimeoutSeconds", 30);
        s.nonceTtlSec       = sConfigMgr->GetOption<uint32>("Voice.NonceTtlSeconds", 60);
        s.nearbyIntervalMs  = std::max<uint32>(200, sConfigMgr->GetOption<uint32>("Voice.NearbyIntervalMs", 1000));
        s.serverCulling     = sConfigMgr->GetOption<bool>("Voice.ServerCulling", true);
        s.muteChatMuted     = sConfigMgr->GetOption<bool>("Voice.MuteChatMuted", true);
        s.groupAlwaysAudible = sConfigMgr->GetOption<bool>("Voice.GroupAlwaysAudible", true);

        s.bot.host          = sConfigMgr->GetOption<std::string>("Voice.Bot.Host", "127.0.0.1");
        s.bot.port          = uint16(sConfigMgr->GetOption<uint32>("Voice.Bot.Port", 64738));
        s.bot.username      = sConfigMgr->GetOption<std::string>("Voice.Bot.Username", "SuperUser");
        s.bot.password      = sConfigMgr->GetOption<std::string>("Voice.Bot.Password", "");
        s.bot.certPinSha256 = sConfigMgr->GetOption<std::string>("Voice.Bot.CertSha256", s.certSha256);
        s.bot.rootChannel   = sConfigMgr->GetOption<std::string>("Voice.Bot.RootChannel", "WoW");
        s.bot.lobbyChannel  = sConfigMgr->GetOption<std::string>("Voice.Bot.LobbyChannel", "Lobby");
        s.bot.emptyChannelTimeoutSec = sConfigMgr->GetOption<uint32>("Voice.Bot.EmptyChannelTimeout", 120);
        s.bot.verbose       = sConfigMgr->GetOption<bool>("Voice.Bot.Verbose", false);
        s.bot.messageRate   = sConfigMgr->GetOption<float>("Voice.Bot.MessageRate", 0.9f);
        s.bot.messageBurst  = sConfigMgr->GetOption<float>("Voice.Bot.MessageBurst", 4.0f);

        std::stringstream ss(sConfigMgr->GetOption<std::string>("Voice.AllowedExternalUsers", ""));
        for (std::string n; std::getline(ss, n, ',');)
        {
            n.erase(0, n.find_first_not_of(" \t"));
            n.erase(n.find_last_not_of(" \t") + 1);
            if (!n.empty())
                s.allowedExternalUsers.push_back(n);
        }
        if (s.maxDistance < s.minDistance + 1.0f)
            s.maxDistance = s.minDistance + 1.0f;

        bool restart = reload && _started;
        if (restart)
            Stop();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _cfg = s;
        }
        if (restart)
            Start();
    }

    void VoiceManager::Start()
    {
        if (_started || !_cfg.enable)
            return;
        if (_cfg.bot.password.empty())
        {
            LOG_ERROR("module", "mod-voicechat: Voice.Bot.Password is empty, voice chat stays disabled");
            return;
        }
        _bot.onLog = [](std::string const& m) { LOG_INFO("module", "mod-voicechat bot: {}", m); };
        _bot.Start(_cfg.bot);
        _started = true;
        LOG_INFO("module", "mod-voicechat: started (Murmur {}:{}, public {}:{})",
            _cfg.bot.host, _cfg.bot.port, _cfg.publicHost, _cfg.publicPort);
    }

    void VoiceManager::Stop()
    {
        if (!_started)
            return;
        _bot.Stop();
        _started = false;
        std::lock_guard<std::mutex> lock(_mutex);
        _unbound.clear();
        _mumbleNames.clear();
        LOG_INFO("module", "mod-voicechat: stopped");
    }

    // ------------------------------------------------------------------------------------------
    //  Hilfen / helpers
    // ------------------------------------------------------------------------------------------
    std::vector<std::string> VoiceManager::ChannelPath(Player* player) const
    {
        std::vector<std::string> path { _cfg.realm, "Map-" + std::to_string(player->GetMapId()) };
        if (player->GetInstanceId())
            path.push_back("Inst-" + std::to_string(player->GetInstanceId()));
        if (!_cfg.crossFaction)
            path.back() += player->GetTeamId() == TEAM_ALLIANCE ? "-A" : "-H";
        return path;
    }

    std::string VoiceManager::Context(Player* player) const
    {
        std::string c = "wow335|" + _cfg.realm + "|" + std::to_string(player->GetMapId()) + "|" +
                         std::to_string(player->GetInstanceId());
        if (!_cfg.crossFaction)
            c += player->GetTeamId() == TEAM_ALLIANCE ? "|A" : "|H";
        return c;
    }

    bool VoiceManager::IsAllowedExternal(std::string const& name) const
    {
        return std::find(_cfg.allowedExternalUsers.begin(), _cfg.allowedExternalUsers.end(), name) !=
               _cfg.allowedExternalUsers.end();
    }

    VoiceManager::VoicePlayer* VoiceManager::Find(Player* player)
    {
        auto it = _players.find(player->GetGUID().GetCounter());
        return it == _players.end() ? nullptr : &it->second;
    }

    void VoiceManager::SendToClient(Player* player, std::vector<uint8_t> const& mvcp)
    {
        if (!player || !player->GetSession())
            return;
        WorldPacket data(SMSG_VOICE_SESSION_ADJUST_PRIORITY, mvcp.size());
        data.append(mvcp.data(), mvcp.size());
        player->GetSession()->SendPacket(&data);
    }

    void VoiceManager::SendDisabled(Player* player, std::string const& reason)
    {
        VoiceProto::Disabled d;
        d.reason = reason;
        SendToClient(player, VoiceProto::Encode(d));
    }

    void VoiceManager::Unbind(VoicePlayer& vp, std::string const& reason)
    {
        if (!vp.session)
            return;
        if (_bot.IsConnected())
            _bot.Kick(vp.session, reason);
        _bySession.erase(vp.session);
        vp.session = 0;
        vp.nearby.clear();
        vp.group.clear();
        vp.appliedMute = false;
    }

    // ------------------------------------------------------------------------------------------
    //  WoW -> Server
    // ------------------------------------------------------------------------------------------
    bool VoiceManager::HandleClientPacket(WorldSession* session, WorldPacket const& packet)
    {
        // DE: Format: [u8 voice][u8 mic] + MVCP. Das Original-Paket hat nur 2 Byte.
        // EN: format: [u8 voice][u8 mic] + MVCP. The original packet has just 2 bytes.
        if (packet.GetOpcode() != CMSG_VOICE_SESSION_ENABLE || packet.size() < 7 ||
            std::memcmp(packet.contents() + 2, VoiceProto::MAGIC, 4) != 0)
            return false;

        VoiceProto::Msg type;
        uint8_t const* body = nullptr;
        size_t bodyLen = 0;
        if (!VoiceProto::Unwrap(packet.contents() + 2, packet.size() - 2, type, body, bodyLen))
            return true;

        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld())
            return true;   // DE: erst im Spiel. EN: only in world.

        if (type == VoiceProto::Msg::Hello)
        {
            VoiceProto::Hello h;
            if (VoiceProto::Decode(body, bodyLen, h))
                HandleHello(session, player);
        }
        return true;
    }

    void VoiceManager::HandleHello(WorldSession* session, Player* player)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        uint64 now = NowMs();
        ObjectGuid::LowType low = player->GetGUID().GetCounter();
        VoicePlayer* vp = Find(player);
        if (vp && now - vp->lastHello < 2000)
            return;   // DE: Spam-Schutz. EN: spam protection.

        if (!_cfg.enable)
            return SendDisabled(player, "disabled");
        if (uint32(session->GetSecurity()) < _cfg.minSecurity)
            return SendDisabled(player, "permission");
        if (session->HasAccountFlag(ACCOUNT_FLAG_DISABLE_VOICE))
            return SendDisabled(player, "account");
        if (vp && vp->blocked)
            return SendDisabled(player, "kicked");
        if (!_started || !_bot.IsConnected())
            return SendDisabled(player, "unavailable");

        if (!vp)
        {
            vp = &_players[low];
            vp->guid = player->GetGUID();
        }
        vp->lastHello = now;
        vp->username = player->GetName();
        if (_cfg.appendRealmToName)
            vp->username += "@" + _cfg.realm;

        SendConfig(*vp, player);
    }

    void VoiceManager::SendConfig(VoicePlayer& vp, Player* player)
    {
        std::array<uint8, 16> rnd;
        Acore::Crypto::GetRandomBytes(rnd);
        vp.nonce = ByteArrayToHexStr(rnd);
        vp.nonceExpire = NowMs() + uint64(_cfg.nonceTtlSec) * 1000;
        vp.path = ChannelPath(player);
        vp.context = Context(player);

        VoiceProto::Config c;
        c.host = _cfg.publicHost;
        c.port = _cfg.publicPort;
        c.password = _cfg.serverPassword;
        c.certPin = _cfg.certSha256;
        c.nonce = vp.nonce;
        c.username = vp.username;
        c.botName = _cfg.bot.username;
        c.context = vp.context;
        c.minDistance = _cfg.minDistance;
        c.maxDistance = _cfg.maxDistance;
        SendToClient(player, VoiceProto::Encode(c));
        LOG_DEBUG("module", "mod-voicechat: CONFIG -> {}", vp.username);
    }

    // ------------------------------------------------------------------------------------------
    //  Murmur -> Server
    // ------------------------------------------------------------------------------------------
    void VoiceManager::HandleBotEvent(MumbleBot::Event const& e)
    {
        switch (e.type)
        {
            case MumbleBot::Event::Connected:
            {
                LOG_INFO("module", "mod-voicechat: connected to Murmur (session {})", e.session);
                // DE: Session-IDs koennen neu vergeben sein (Murmur-Neustart) -> alle Bindungen verwerfen,
                //     jeder Spieler bekommt eine frische CONFIG und bindet sich per neuer Nonce erneut.
                // EN: session ids may have been reassigned (Murmur restart) -> drop all bindings,
                //     every player gets a fresh CONFIG and re-binds with a new nonce.
                _bySession.clear();
                for (auto& [low, vp] : _players)
                {
                    vp.session = 0;
                    vp.nearby.clear();
                    vp.group.clear();
                    vp.appliedMute = false;
                    if (vp.blocked)
                        continue;
                    if (Player* player = ObjectAccessor::FindConnectedPlayer(vp.guid))
                        if (player->IsInWorld())
                            SendConfig(vp, player);
                }
                break;
            }
            case MumbleBot::Event::Disconnected:
                LOG_WARN("module", "mod-voicechat: lost Murmur connection: {}", e.text);
                _unbound.clear();
                break;
            case MumbleBot::Event::UserJoined:
            {
                if (e.session == _bot.OwnSession())
                    break;
                _mumbleNames[e.session] = e.name;
                if (!IsAllowedExternal(e.name) && !_bySession.count(e.session))
                    _unbound[e.session] = NowMs();
                break;
            }
            case MumbleBot::Event::UserLeft:
            {
                _mumbleNames.erase(e.session);
                _unbound.erase(e.session);
                auto b = _bySession.find(e.session);
                if (b != _bySession.end())
                {
                    auto p = _players.find(b->second);
                    if (p != _players.end())
                    {
                        p->second.session = 0;
                        p->second.nearby.clear();
                        p->second.group.clear();
                        p->second.appliedMute = false;
                    }
                    _bySession.erase(b);
                }
                break;
            }
            case MumbleBot::Event::Text:
            {
                std::string nonce;
                if (VoiceProto::ParseBindText(e.text, nonce))
                    Bind(e.session, nonce);
                break;
            }
        }
    }

    void VoiceManager::Bind(uint32 session, std::string const& nonce)
    {
        uint64 now = NowMs();
        auto nameIt = _mumbleNames.find(session);
        if (nameIt == _mumbleNames.end())
            return;

        for (auto& [low, vp] : _players)
        {
            if (vp.nonce.empty() || vp.nonce != nonce)
                continue;
            vp.nonce.clear();   // DE: Einmal-Nonce. EN: one-time nonce.
            if (now > vp.nonceExpire || nameIt->second != vp.username)
            {
                LOG_WARN("module", "mod-voicechat: rejected BIND from '{}' (expected '{}', expired {})",
                    nameIt->second, vp.username, now > vp.nonceExpire);
                _bot.Kick(session, "voice binding rejected");
                return;
            }
            Player* player = ObjectAccessor::FindConnectedPlayer(vp.guid);
            if (!player)
                return;
            if (vp.session && vp.session != session)
                Unbind(vp, "replaced by a new voice session");

            vp.session = session;
            vp.listsDirty = true;
            vp.nearby.clear();
            vp.group.clear();
            vp.appliedMute = false;
            _bySession[session] = low;
            _unbound.erase(session);

            Refresh(vp, player, true);
            VoiceProto::Bound b;
            b.session = session;
            SendToClient(player, VoiceProto::Encode(b));
            LOG_INFO("module", "mod-voicechat: {} bound to Mumble session {}", vp.username, session);
            return;
        }
        // DE: Unbekannte Nonce -> ignorieren, der Bind-Timeout kickt spaeter.
        // EN: unknown nonce -> ignore, the bind timeout kicks later.
    }

    // ------------------------------------------------------------------------------------------
    //  Zustand / state
    // ------------------------------------------------------------------------------------------
    void VoiceManager::ApplyMute(VoicePlayer& vp, Player* player)
    {
        if (!vp.session)
            return;
        bool mute = vp.muted || player->GetSession()->HasAccountFlag(ACCOUNT_FLAG_DISABLE_VOICE_SPEAK) ||
                    (_cfg.muteChatMuted && !player->CanSpeak());
        if (mute == vp.appliedMute)
            return;
        _bot.SetMute(vp.session, mute);
        vp.appliedMute = mute;
    }

    void VoiceManager::Refresh(VoicePlayer& vp, Player* player, bool force)
    {
        vp.dirty = false;
        std::vector<std::string> path = ChannelPath(player);
        std::string context = Context(player);

        if (context != vp.context || force)
        {
            vp.context = context;
            VoiceProto::Context c;
            c.context = context;
            c.seq = ++vp.contextSeq;
            SendToClient(player, VoiceProto::Encode(c));
        }
        if (vp.session && (path != vp.path || force))
        {
            vp.path = path;
            vp.listsDirty = true;
            _bot.MoveUser(vp.session, path);
        }
        else
            vp.path = path;
        ApplyMute(vp, player);
    }

    void VoiceManager::UpdateNearby()
    {
        // DE: Gebundene Spieler je Map/Instanz sammeln. EN: collect bound players per map/instance.
        struct Entry { VoicePlayer* vp; Player* player; std::vector<uint32> near, group; };
        std::map<std::pair<uint32, uint32>, std::vector<Entry>> groups;
        std::vector<ObjectGuid::LowType> gone;

        for (auto& [low, vp] : _players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(vp.guid);
            if (!player)
            {
                gone.push_back(low);
                continue;
            }
            if (!player->IsInWorld())
                continue;   // DE: Teleport laeuft. EN: teleport in progress.
            if (vp.dirty || Context(player) != vp.context)
                Refresh(vp, player, false);
            else
                ApplyMute(vp, player);
            if (vp.session)
                groups[{ player->GetMapId(), player->GetInstanceId() }].push_back({ &vp, player, {}, {} });
        }
        for (ObjectGuid::LowType low : gone)
        {
            Unbind(_players[low], "logged out");
            _players.erase(low);
        }

        float range = _cfg.maxDistance * 1.1f;
        for (auto& [key, list] : groups)
        {
            // DE: Gruppe/Raid: immer hoerbar, auch auf anderen Maps. EN: party/raid: always audible, even on other maps.
            if (_cfg.groupAlwaysAudible)
                for (Entry& e : list)
                    if (Group* grp = e.player->GetGroup())
                        for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
                        {
                            Player* member = ref->GetSource();
                            if (!member || member == e.player)
                                continue;
                            auto it = _players.find(member->GetGUID().GetCounter());
                            if (it != _players.end() && it->second.session)
                                e.group.push_back(it->second.session);
                        }

            // DE: Fremde in Hoerweite (ohne ServerCulling: alle auf der Map/Instanz).
            // EN: strangers in range (without ServerCulling: everyone on the map/instance).
            for (size_t i = 0; i < list.size(); ++i)
                for (size_t j = i + 1; j < list.size(); ++j)
                {
                    Player* a = list[i].player;
                    Player* b = list[j].player;
                    if (!_cfg.crossFaction && a->GetTeamId() != b->GetTeamId())
                        continue;
                    if (!a->InSamePhase(b) || (_cfg.serverCulling && a->GetExactDist(b) > range))
                        continue;
                    list[i].near.push_back(list[j].vp->session);
                    list[j].near.push_back(list[i].vp->session);
                }

            for (Entry& e : list)
            {
                std::sort(e.group.begin(), e.group.end());
                std::sort(e.near.begin(), e.near.end());
                // DE: Gruppe hat Vorrang. EN: group takes precedence.
                e.near.erase(std::remove_if(e.near.begin(), e.near.end(), [&](uint32 s) {
                    return std::binary_search(e.group.begin(), e.group.end(), s); }), e.near.end());
                if (!e.vp->listsDirty && e.near == e.vp->nearby && e.group == e.vp->group)
                    continue;
                e.vp->listsDirty = false;
                e.vp->nearby = e.near;
                e.vp->group = e.group;
                VoiceProto::Nearby n;
                n.sessions = e.near;
                n.group = e.group;
                SendToClient(e.player, VoiceProto::Encode(n));
            }
        }
    }

    void VoiceManager::Update(uint32 diff)
    {
        if (!_started)
            return;
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto const& e : _bot.PollEvents())
            HandleBotEvent(e);

        uint64 now = NowMs();
        // DE: Nicht gebundene Mumble-Nutzer kicken. EN: kick unbound Mumble users.
        for (auto it = _unbound.begin(); it != _unbound.end();)
        {
            if (now - it->second > uint64(_cfg.bindTimeoutSec) * 1000)
            {
                _bot.Kick(it->first, "no World of Warcraft voice binding");
                it = _unbound.erase(it);
            }
            else
                ++it;
        }
        for (auto& [low, vp] : _players)
            if (!vp.nonce.empty() && now > vp.nonceExpire)
                vp.nonce.clear();

        _nearbyTimer += diff;
        if (_nearbyTimer >= _cfg.nearbyIntervalMs)
        {
            _nearbyTimer = 0;
            UpdateNearby();
        }
    }

    // ------------------------------------------------------------------------------------------
    //  Hooks + GM
    // ------------------------------------------------------------------------------------------
    void VoiceManager::OnLogout(Player* player)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _players.find(player->GetGUID().GetCounter());
        if (it == _players.end())
            return;
        Unbind(it->second, "logged out");
        _players.erase(it);
    }

    void VoiceManager::OnMapChanged(Player* player)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (VoicePlayer* vp = Find(player))
            vp->dirty = true;   // DE: Update() erledigt den Rest. EN: Update() does the rest.
    }

    bool VoiceManager::SetMute(Player* player, bool mute)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        VoicePlayer* vp = Find(player);
        if (!vp)
            return false;
        vp->muted = mute;
        ApplyMute(*vp, player);
        return true;
    }

    bool VoiceManager::Kick(Player* player)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        VoicePlayer* vp = Find(player);
        if (!vp)
            return false;
        vp->blocked = true;
        vp->nonce.clear();
        Unbind(*vp, "kicked from voice chat by a GM");
        SendDisabled(player, "kicked");
        return true;
    }

    std::string VoiceManager::Status()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        size_t bound = 0;
        for (auto const& p : _players)
            bound += p.second.session ? 1 : 0;
        std::ostringstream o;
        o << "enabled=" << (_cfg.enable ? "yes" : "no")
          << " murmur=" << (_started && _bot.IsConnected() ? "connected" : "disconnected")
          << " players=" << _players.size() << " bound=" << bound << " unbound=" << _unbound.size();
        return o.str();
    }
}
