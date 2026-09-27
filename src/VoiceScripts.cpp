/*
 * mod-voicechat - AzerothCore hooks + GM commands
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Kein Core-Patch: Das MVCP-Paket (CMSG_VOICE_SESSION_ENABLE mit Zusatzdaten)
 *     wird per ServerScript::CanPacketReceive abgefangen, bevor der Dummy-Handler
 *     des Cores es sieht. Alles andere laeuft ueber normale Script-Hooks.
 * EN: No core patch: the MVCP packet (CMSG_VOICE_SESSION_ENABLE with extra data)
 *     is intercepted via ServerScript::CanPacketReceive before the core's dummy
 *     handler sees it. Everything else uses regular script hooks.
 */
#include "VoiceManager.h"

#include "AllMapScript.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Player.h"
#include "PlayerScript.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "ServerScript.h"
#include "WorldScript.h"

using namespace Acore::ChatCommands;

class VoiceChat_WorldScript : public WorldScript
{
public:
    VoiceChat_WorldScript() : WorldScript("VoiceChat_WorldScript", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_SHUTDOWN, WORLDHOOK_ON_UPDATE }) { }

    void OnAfterConfigLoad(bool reload) override { sVoiceManager->LoadConfig(reload); }
    void OnStartup() override { sVoiceManager->Start(); }
    void OnShutdown() override { sVoiceManager->Stop(); }
    void OnUpdate(uint32 diff) override { sVoiceManager->Update(diff); }
};

class VoiceChat_ServerScript : public ServerScript
{
public:
    VoiceChat_ServerScript() : ServerScript("VoiceChat_ServerScript", { SERVERHOOK_CAN_PACKET_RECEIVE }) { }

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        // DE: false = Core verwirft das Paket (wir haben es verarbeitet). EN: false = core drops the packet (we handled it).
        return !sVoiceManager->HandleClientPacket(session, packet);
    }
};

class VoiceChat_PlayerScript : public PlayerScript
{
public:
    VoiceChat_PlayerScript() : PlayerScript("VoiceChat_PlayerScript", { PLAYERHOOK_ON_LOGOUT }) { }

    void OnPlayerLogout(Player* player) override { sVoiceManager->OnLogout(player); }
};

class VoiceChat_AllMapScript : public AllMapScript
{
public:
    VoiceChat_AllMapScript() : AllMapScript("VoiceChat_AllMapScript", { ALLMAPHOOK_ON_PLAYER_ENTER_ALL }) { }

    void OnPlayerEnterAll(Map* /*map*/, Player* player) override { sVoiceManager->OnMapChanged(player); }
};

class VoiceChat_CommandScript : public CommandScript
{
public:
    VoiceChat_CommandScript() : CommandScript("VoiceChat_CommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable voiceTable =
        {
            { "status", HandleStatus,                   rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            { "mute",   HandleMute,                     rbac::RBAC_PERM_COMMAND_MUTE,        Console::Yes },
            { "unmute", HandleUnmute,                   rbac::RBAC_PERM_COMMAND_MUTE,        Console::Yes },
            { "kick",   HandleKick,                     rbac::RBAC_PERM_COMMAND_KICK,        Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "voice", voiceTable },
        };
        return commandTable;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        handler->PSendSysMessage("mod-voicechat: {}", sVoiceManager->Status());
        return true;
    }

    static Player* Target(ChatHandler* handler, Optional<PlayerIdentifier>& target)
    {
        if (!target)
            target = PlayerIdentifier::FromTargetOrSelf(handler);
        Player* player = target ? target->GetConnectedPlayer() : nullptr;
        if (!player)
            handler->SendErrorMessage("mod-voicechat: player not found / Spieler nicht gefunden");
        return player;
    }

    static bool Mute(ChatHandler* handler, Optional<PlayerIdentifier> target, bool mute)
    {
        Player* player = Target(handler, target);
        if (!player)
            return false;
        if (!sVoiceManager->SetMute(player, mute))
        {
            handler->SendErrorMessage("mod-voicechat: {} does not use voice chat", player->GetName());
            return false;
        }
        handler->PSendSysMessage("mod-voicechat: {} {}", player->GetName(), mute ? "muted" : "unmuted");
        return true;
    }

    static bool HandleMute(ChatHandler* handler, Optional<PlayerIdentifier> target) { return Mute(handler, target, true); }
    static bool HandleUnmute(ChatHandler* handler, Optional<PlayerIdentifier> target) { return Mute(handler, target, false); }

    static bool HandleKick(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        Player* player = Target(handler, target);
        if (!player)
            return false;
        if (!sVoiceManager->Kick(player))
        {
            handler->SendErrorMessage("mod-voicechat: {} does not use voice chat", player->GetName());
            return false;
        }
        handler->PSendSysMessage("mod-voicechat: {} kicked from voice until relog", player->GetName());
        return true;
    }
};

void AddVoiceChatScripts()
{
    new VoiceChat_WorldScript();
    new VoiceChat_ServerScript();
    new VoiceChat_PlayerScript();
    new VoiceChat_AllMapScript();
    new VoiceChat_CommandScript();
}
