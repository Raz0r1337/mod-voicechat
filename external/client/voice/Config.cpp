/*
 * mod-voicechat - voice.ini + voice.log
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "Config.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>

namespace voice
{
    namespace
    {
        std::mutex g_logMutex;
        bool g_logEnabled = true;

        std::string Get(const std::string& path, const char* sec, const char* key, const std::string& def)
        {
            char buf[512];
            GetPrivateProfileStringA(sec, key, def.c_str(), buf, sizeof(buf), path.c_str());
            return buf;
        }

        float GetF(const std::string& path, const char* sec, const char* key, float def)
        {
            std::string v = Get(path, sec, key, "");
            return v.empty() ? def : float(std::atof(v.c_str()));
        }

    }

    std::string Config::ModuleDir()
    {
        // DE: Ordner der Wow.exe. EN: folder of Wow.exe.
        char path[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string p = path;
        size_t slash = p.find_last_of("\\/");
        return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
    }

    bool Config::Load(const std::string& path)
    {
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            return false;
        serverMode = Get(path, "Server", "Mode", serverMode);
        host = Get(path, "Server", "Host", host);
        port = uint16_t(GetPrivateProfileIntA("Server", "Port", port, path.c_str()));
        password = Get(path, "Server", "Password", password);
        certSha256 = Get(path, "Server", "CertSha256", certSha256);
        forceTcp = GetPrivateProfileIntA("Server", "ForceTcp", forceTcp, path.c_str()) != 0;

        username = Get(path, "Player", "Username", username);
        autoConnectInWorld = GetPrivateProfileIntA("Player", "AutoConnectInWorld", autoConnectInWorld, path.c_str()) != 0;

        mode = Get(path, "Audio", "Mode", mode);
        std::string key = Get(path, "Audio", "PushToTalkKey", "");
        if (!key.empty()) { int k = ParseKey(key); if (k > 0) pushToTalkKey = k; }
        vadThreshold = GetF(path, "Audio", "VadThreshold", vadThreshold);
        inputGain = GetF(path, "Audio", "InputGain", inputGain);
        outputVolume = GetF(path, "Audio", "OutputVolume", outputVolume);
        bitrate = GetPrivateProfileIntA("Audio", "Bitrate", bitrate, path.c_str());
        inputDevice = Get(path, "Audio", "InputDevice", inputDevice);
        outputDevice = Get(path, "Audio", "OutputDevice", outputDevice);

        minDistance = GetF(path, "Spatial", "MinDistance", minDistance);
        maxDistance = GetF(path, "Spatial", "MaxDistance", maxDistance);
        listenerMode = Get(path, "Spatial", "ListenerMode", listenerMode);
        hearWithoutPosition = GetPrivateProfileIntA("Spatial", "HearWithoutPosition", hearWithoutPosition, path.c_str()) != 0;

        log = GetPrivateProfileIntA("Debug", "Log", log, path.c_str()) != 0;
        return true;
    }

    int ParseKey(const std::string& raw)
    {
        std::string n;
        for (char c : raw) if (c != ' ') n += char(std::toupper(static_cast<unsigned char>(c)));
        if (n.rfind("0X", 0) == 0) return int(std::strtol(n.c_str(), nullptr, 16));
        if (n.size() == 1 && ((n[0] >= 'A' && n[0] <= 'Z') || (n[0] >= '0' && n[0] <= '9'))) return n[0];
        if (n.size() >= 2 && n[0] == 'F' && std::isdigit(static_cast<unsigned char>(n[1])))
        {
            int f = std::atoi(n.c_str() + 1);
            if (f >= 1 && f <= 24) return VK_F1 + f - 1;
        }
        struct { const char* name; int vk; } names[] = {
            { "CAPSLOCK", VK_CAPITAL }, { "SPACE", VK_SPACE }, { "TAB", VK_TAB },
            { "LSHIFT", VK_LSHIFT }, { "RSHIFT", VK_RSHIFT }, { "LCTRL", VK_LCONTROL }, { "RCTRL", VK_RCONTROL },
            { "LALT", VK_LMENU }, { "RALT", VK_RMENU }, { "MOUSE3", VK_MBUTTON }, { "MOUSE4", VK_XBUTTON1 },
            { "MOUSE5", VK_XBUTTON2 }, { "BACKQUOTE", VK_OEM_3 }, { "INSERT", VK_INSERT }, { "HOME", VK_HOME },
            { "PAGEUP", VK_PRIOR }, { "PAGEDOWN", VK_NEXT }, { "END", VK_END },
        };
        for (auto& e : names) if (n == e.name) return e.vk;
        return int(std::strtol(n.c_str(), nullptr, 10));
    }

    void SetLogEnabled(bool on) { g_logEnabled = on; }

    void Log(const std::string& msg)
    {
        if (!g_logEnabled) return;
        std::lock_guard<std::mutex> g(g_logMutex);
        static std::string path = Config::ModuleDir() + "\\voice.log";
        static bool rotated = false;
        if (!rotated)
        {
            // DE: Groesser als 1 MB -> nach voice.old.log verschieben. EN: larger than 1 MB -> move to voice.old.log.
            rotated = true;
            WIN32_FILE_ATTRIBUTE_DATA fa;
            if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fa) && (fa.nFileSizeHigh || fa.nFileSizeLow > 1024 * 1024))
            {
                std::string old = Config::ModuleDir() + "\\voice.old.log";
                DeleteFileA(old.c_str());
                MoveFileA(path.c_str(), old.c_str());
            }
        }
        FILE* f = std::fopen(path.c_str(), "a");
        if (!f) return;
        std::time_t t = std::time(nullptr);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        std::fprintf(f, "%s %s\n", ts, msg.c_str());
        std::fclose(f);
    }
}
