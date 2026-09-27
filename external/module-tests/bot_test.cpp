/*
 * mod-voicechat - bot_test: drives the MumbleBot like VoiceManager would (without AzerothCore)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Verbindet den Bot, nimmt BIND-Texte an und verschiebt die Nutzer in die
 *     per --assign vorgegebenen Channels. Nicht gebundene Nutzer werden nach
 *     --kick-unbound Sekunden gekickt.
 * EN: Connects the bot, accepts BIND texts and moves users into the channels
 *     given via --assign. Unbound users are kicked after --kick-unbound seconds.
 *
 *   bot_test --password pw --assign Alice=Realm/Map-0 --assign Bob=Realm/Map-1/Inst-5 --seconds 20
 */
#include "MumbleBot.h"
#include "VoiceProtocol.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <thread>

using namespace VoiceChat;
using Clock = std::chrono::steady_clock;

int main(int argc, char** argv)
{
    MumbleBot::Config cfg;
    double seconds = 20, kickUnbound = 0;
    std::map<std::string, std::vector<std::string>> assign;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--host") cfg.host = next();
        else if (a == "--port") cfg.port = uint16_t(std::atoi(next().c_str()));
        else if (a == "--password") cfg.password = next();
        else if (a == "--seconds") seconds = std::atof(next().c_str());
        else if (a == "--verbose") cfg.verbose = true;
        else if (a == "--kick-unbound") kickUnbound = std::atof(next().c_str());
        else if (a == "--assign")
        {
            std::string v = next();
            size_t eq = v.find('=');
            std::vector<std::string> path;
            std::stringstream ss(v.substr(eq + 1));
            for (std::string seg; std::getline(ss, seg, '/');) path.push_back(seg);
            assign[v.substr(0, eq)] = path;
        }
    }

    MumbleBot bot;
    bot.onLog = [](const std::string& m) { std::printf("[bot] %s\n", m.c_str()); std::fflush(stdout); };
    bot.Start(cfg);

    std::map<uint32_t, std::string> names;
    std::map<uint32_t, Clock::time_point> joined;
    std::map<uint32_t, bool> bound;
    int errors = 0;
    auto end = Clock::now() + std::chrono::milliseconds(int(seconds * 1000));

    while (Clock::now() < end)
    {
        for (auto& e : bot.PollEvents())
        {
            switch (e.type)
            {
                case MumbleBot::Event::Connected: std::printf("[test] bot connected, session %u\n", e.session); break;
                case MumbleBot::Event::Disconnected: std::printf("[test] bot disconnected: %s\n", e.text.c_str()); ++errors; break;
                case MumbleBot::Event::UserJoined:
                    names[e.session] = e.name; joined[e.session] = Clock::now();
                    std::printf("[test] user joined: %s (%u)\n", e.name.c_str(), e.session);
                    break;
                case MumbleBot::Event::UserLeft: std::printf("[test] user left: %u\n", e.session); names.erase(e.session); break;
                case MumbleBot::Event::Text:
                {
                    std::string nonce;
                    if (!VoiceProto::ParseBindText(e.text, nonce)) break;
                    const std::string& name = names[e.session];
                    auto it = assign.find(name);
                    if (it == assign.end()) { std::printf("[test] no assignment for %s\n", name.c_str()); break; }
                    // DE: Im echten Modul wird hier die Nonce geprueft. EN: the real module verifies the nonce here.
                    bound[e.session] = true;
                    std::printf("[test] BIND %s -> moving\n", name.c_str());
                    bot.MoveUser(e.session, it->second);
                    bot.SendText(e.session, "MVC1-BOUND");
                    break;
                }
            }
        }
        if (kickUnbound > 0)
            for (auto& j : joined)
                if (!bound[j.first] && names.count(j.first) &&
                    Clock::now() - j.second > std::chrono::milliseconds(int(kickUnbound * 1000)))
                {
                    std::printf("[test] kicking unbound %s\n", names[j.first].c_str());
                    bot.Kick(j.first, "no voice binding");
                    bound[j.first] = true;   // nur einmal / only once
                }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    bot.Stop();
    std::printf("[test] done, %d errors\n", errors);
    return errors ? 1 : 0;
}
