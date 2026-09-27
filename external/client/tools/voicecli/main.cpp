/*
 * mod-voicechat - voicecli: headless Mumble test client
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Sendet einen Sinuston und/oder prueft, ob ein Ton ankommt (Goertzel).
 *     Exit-Code 0 = Erwartung erfuellt. Fuer automatische Ende-zu-Ende-Tests.
 * EN: Sends a sine tone and/or verifies that a tone arrives (Goertzel).
 *     Exit code 0 = expectation met. For automated end-to-end tests.
 *
 *   voicecli --user A --send-tone 440 --seconds 4
 *   voicecli --user B --expect-tone 440 --seconds 7 [--wav out.wav] [--tcp]
 */
#include "voicecore/AudioMixer.h"
#include "voicecore/MumbleClient.h"
#include "voicecore/Transmitter.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace voicecore;
using Clock = std::chrono::steady_clock;

namespace
{
    constexpr double PI = 3.14159265358979323846;

    // DE: Anteil der Energie bei freq. EN: share of energy at freq.
    double ToneRatio(const float* x, int n, double freq)
    {
        double k = 2.0 * std::cos(2.0 * PI * freq / SAMPLE_RATE), s1 = 0, s2 = 0, energy = 0;
        for (int i = 0; i < n; ++i)
        {
            double s0 = x[i] + k * s1 - s2;
            s2 = s1; s1 = s0;
            energy += double(x[i]) * x[i];
        }
        double power = s1 * s1 + s2 * s2 - k * s1 * s2;
        return energy > 0 ? (2.0 * power / n) / energy : 0.0;
    }

    void WriteWav(const std::string& path, const std::vector<float>& stereo)
    {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return;
        uint32_t dataBytes = uint32_t(stereo.size() * 2), v32;
        uint16_t v16;
        std::fwrite("RIFF", 1, 4, f); v32 = 36 + dataBytes; std::fwrite(&v32, 4, 1, f);
        std::fwrite("WAVEfmt ", 1, 8, f); v32 = 16; std::fwrite(&v32, 4, 1, f);
        v16 = 1; std::fwrite(&v16, 2, 1, f); v16 = 2; std::fwrite(&v16, 2, 1, f);
        v32 = SAMPLE_RATE; std::fwrite(&v32, 4, 1, f); v32 = SAMPLE_RATE * 4; std::fwrite(&v32, 4, 1, f);
        v16 = 4; std::fwrite(&v16, 2, 1, f); v16 = 16; std::fwrite(&v16, 2, 1, f);
        std::fwrite("data", 1, 4, f); std::fwrite(&dataBytes, 4, 1, f);
        for (float s : stereo) { int16_t i = int16_t(std::lround(s * 32767.0f)); std::fwrite(&i, 2, 1, f); }
        std::fclose(f);
    }
}

int main(int argc, char** argv)
{
    ClientConfig cfg;
    cfg.username = "voicecli";
    double sendTone = 0, expectTone = 0, seconds = 5;
    std::string wav;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--host") cfg.host = next();
        else if (a == "--port") cfg.port = uint16_t(std::atoi(next().c_str()));
        else if (a == "--user") cfg.username = next();
        else if (a == "--password") cfg.password = next();
        else if (a == "--pin") cfg.certPinSha256 = next();
        else if (a == "--tcp") cfg.forceTcp = true;
        else if (a == "--send-tone") sendTone = std::atof(next().c_str());
        else if (a == "--expect-tone") expectTone = std::atof(next().c_str());
        else if (a == "--seconds") seconds = std::atof(next().c_str());
        else if (a == "--wav") wav = next();
        else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }

    MumbleClient client;
    AudioMixer mixer;
    Transmitter tx;
    std::map<uint32_t, int> packetsFrom;

    client.onLog = [&](const std::string& m) { std::printf("[%s] %s\n", cfg.username.c_str(), m.c_str()); std::fflush(stdout); };
    client.onDisconnected = [&](const std::string& r) { std::printf("[%s] disconnected: %s\n", cfg.username.c_str(), r.c_str()); };
    client.onAudio = [&](const MumbleProto::UdpAudio& a) {
        ++packetsFrom[a.senderSession];
        mixer.Push(a.senderSession, a.frameNumber, a.opus, a.isTerminator);
    };

    if (!tx.Init())
    {
        std::fprintf(stderr, "opus init failed\n");
        return 1;
    }
    tx.onPacket = [&](const uint8_t* p, size_t n, bool term) { client.SendAudio(p, n, term, nullptr); };

    client.Start(cfg);
    auto deadline = Clock::now() + std::chrono::seconds(10);
    while (client.State() != ClientState::Connected && Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (client.State() != ClientState::Connected)
    {
        std::printf("[%s] FAIL: not connected\n", cfg.username.c_str());
        return 1;
    }

    if (sendTone > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1000));   // Gegenstelle abwarten / wait for peer
    tx.SetMode(sendTone > 0 ? TransmitMode::Continuous : TransmitMode::PushToTalk);

    std::vector<float> record, frame(FRAME_SAMPLES), mix(FRAME_SAMPLES * 2), mono(FRAME_SAMPLES);
    int toneFrames = 0, loudFrames = 0;
    double phase = 0;
    int totalFrames = int(seconds * 50);
    auto tick = Clock::now();

    for (int f = 0; f < totalFrames; ++f)
    {
        tick += std::chrono::milliseconds(20);
        if (sendTone > 0)
        {
            if (f == totalFrames - 1) tx.SetMode(TransmitMode::PushToTalk);   // letzter Frame -> Terminator / last frame -> terminator
            for (int i = 0; i < FRAME_SAMPLES; ++i) { frame[size_t(i)] = float(0.3 * std::sin(phase)); phase += 2.0 * PI * sendTone / SAMPLE_RATE; }
            tx.PushPcm(frame.data(), frame.size());
        }

        mixer.Mix(mix.data(), FRAME_SAMPLES, nullptr);
        double e = 0;
        for (int i = 0; i < FRAME_SAMPLES; ++i) { mono[size_t(i)] = mix[size_t(2 * i)]; e += double(mono[size_t(i)]) * mono[size_t(i)]; }
        double rms = std::sqrt(e / FRAME_SAMPLES);
        if (rms > 0.05)
        {
            ++loudFrames;
            if (expectTone > 0 && ToneRatio(mono.data(), FRAME_SAMPLES, expectTone) > 0.8) ++toneFrames;
        }
        if (!wav.empty()) record.insert(record.end(), mix.begin(), mix.end());
        std::this_thread::sleep_until(tick);
    }

    auto st = client.GetStats();
    std::printf("[%s] udp=%s sent(udp=%u tunnel=%u) recv(udp=%u tunnel=%u) crypt(good=%u lost=%u late=%u) loudFrames=%d toneFrames=%d users=%zu\n",
        cfg.username.c_str(), client.UdpActive() ? "yes" : "no", st.udpSent, st.tcpTunnelSent, st.udpRecv, st.tcpTunnelRecv,
        st.cryptGood, st.cryptLost, st.cryptLate, loudFrames, toneFrames, client.Users().size());
    for (auto& kv : packetsFrom) std::printf("[%s]   packets from session %u: %d\n", cfg.username.c_str(), kv.first, kv.second);

    if (!wav.empty()) WriteWav(wav, record);
    client.Stop();

    if (expectTone > 0)
    {
        bool ok = toneFrames >= 50;   // >= 1 s sauberer Ton / clean tone
        std::printf("[%s] %s: %d clean %.0f Hz frames\n", cfg.username.c_str(), ok ? "PASS" : "FAIL", toneFrames, expectTone);
        return ok ? 0 : 1;
    }
    return 0;
}
