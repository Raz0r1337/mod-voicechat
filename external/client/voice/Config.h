/*
 * mod-voicechat - voice.ini
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <cstdint>
#include <string>

namespace voice
{
    struct Config
    {
        // [Server]
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

        // [Wow]  DE: Hypothesen aus wow3.dll (Build 12340). EN: hypotheses from wow3.dll (build 12340)
        uintptr_t inWorldAddress = 0x00BD0792;
        uintptr_t nameAddress = 0x00C79D18;

        // [Debug]
        bool log = true;

        static std::string ModuleDir();
        bool Load(const std::string& path);
    };

    int ParseKey(const std::string& name);
    void Log(const std::string& msg);
    void SetLogEnabled(bool on);
}
