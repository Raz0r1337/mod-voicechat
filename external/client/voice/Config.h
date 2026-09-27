/*
 * mod-voicechat - voice.ini
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace voice
{
    struct Config
    {
        // [Server]
        std::string serverMode = "auto"; // auto | server | standalone
        std::string host = "127.0.0.1";
        uint16_t port = 64738;
        std::string password;
        std::string certSha256;
        bool forceTcp = false;

        // [Player]
        std::string username;            // leer = Charaktername / empty = character name
        bool autoConnectInWorld = true;

        // [Audio]
        std::string mode = "ptt";        // ptt | vad | continuous
        int pushToTalkKey = 0x14;        // VK_CAPITAL
        float vadThreshold = 0.02f;
        float inputGain = 1.0f;
        float outputVolume = 1.0f;
        int bitrate = 32000;
        std::string inputDevice;         // Teilstring, leer = Standard / substring, empty = default
        std::string outputDevice;

        // [Spatial]
        float minDistance = 3.0f;        // Yards: volle Lautstaerke / full volume
        float maxDistance = 40.0f;       // Yards: ab hier stumm / silent from here
        std::string listenerMode = "camera";   // camera | character
        bool hearWithoutPosition = false;

        // [Occlusion]
        bool occlusion = true;
        float occlusionGain = 0.35f;     // Pegel bei voller Verdeckung / level when fully occluded
        float occlusionLowpassHz = 1000.0f;
        uint32_t occlusionFlags = 0x100111;
        float occlusionHeadHeight = 1.5f;
        int occlusionRaysPerTick = 9;

        // [Ui]
        bool nativeUi = true;            // Blizzard-Voice-UI nutzen, wenn der Server es erlaubt / use the Blizzard voice UI if the server allows

        // [Debug]
        bool log = true;

        static std::string ModuleDir();
        bool Load(const std::string& path);
    };

    int ParseKey(const std::string& name);
    // DE: WoW-Tastenbelegung ("LCTRL-F", "BUTTON4", "`") -> virtuelle Tasten (alle muessen gedrueckt sein).
    // EN: WoW key binding ("LCTRL-F", "BUTTON4", "`") -> virtual keys (all must be down).
    std::vector<int> ParseWowBinding(const std::string& binding);
    void Log(const std::string& msg);
    void SetLogEnabled(bool on);
}
