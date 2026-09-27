/*
 * mod-voicechat - thin libopus wrappers (48 kHz mono)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <cstddef>
#include <cstdint>

struct OpusEncoder;
struct OpusDecoder;

namespace voicecore
{
    constexpr int SAMPLE_RATE = 48000;
    constexpr int FRAME_SAMPLES = 960;          // 20 ms
    constexpr int MAX_OPUS_PACKET = 1000;

    class OpusEnc
    {
    public:
        ~OpusEnc();
        bool Init(int bitrate = 32000);
        void SetBitrate(int bitrate);
        // DE: pcm = FRAME_SAMPLES Samples, Rueckgabe = Bytes (<=0 Fehler). EN: returns bytes (<=0 error).
        int Encode(const float* pcm, uint8_t* out, int maxBytes);
    private:
        OpusEncoder* _enc = nullptr;
    };

    class OpusDec
    {
    public:
        ~OpusDec();
        bool Init();
        // DE: data == nullptr -> Paketverlust-Kaschierung (PLC). Rueckgabe = Samples.
        // EN: data == nullptr -> packet loss concealment (PLC). Returns samples.
        int Decode(const uint8_t* data, size_t len, float* pcm, int maxSamples);
        static int PacketSamples(const uint8_t* data, size_t len);
    private:
        OpusDecoder* _dec = nullptr;
    };
}
