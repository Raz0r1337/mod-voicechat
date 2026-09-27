/*
 * mod-voicechat - thin libopus wrappers
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "voicecore/OpusCodec.h"

#include <opus.h>

namespace voicecore
{
    OpusEnc::~OpusEnc() { if (_enc) opus_encoder_destroy(_enc); }

    bool OpusEnc::Init(int bitrate)
    {
        int err = 0;
        _enc = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &err);
        if (err != OPUS_OK || !_enc) return false;
        opus_encoder_ctl(_enc, OPUS_SET_BITRATE(bitrate));
        opus_encoder_ctl(_enc, OPUS_SET_INBAND_FEC(1));
        opus_encoder_ctl(_enc, OPUS_SET_PACKET_LOSS_PERC(10));
        opus_encoder_ctl(_enc, OPUS_SET_COMPLEXITY(8));
        opus_encoder_ctl(_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        return true;
    }

    void OpusEnc::SetBitrate(int bitrate) { if (_enc) opus_encoder_ctl(_enc, OPUS_SET_BITRATE(bitrate)); }

    int OpusEnc::Encode(const float* pcm, uint8_t* out, int maxBytes)
    {
        return _enc ? opus_encode_float(_enc, pcm, FRAME_SAMPLES, out, maxBytes) : -1;
    }

    OpusDec::~OpusDec() { if (_dec) opus_decoder_destroy(_dec); }

    bool OpusDec::Init()
    {
        int err = 0;
        _dec = opus_decoder_create(SAMPLE_RATE, 1, &err);
        return err == OPUS_OK && _dec;
    }

    int OpusDec::Decode(const uint8_t* data, size_t len, float* pcm, int maxSamples)
    {
        if (!_dec) return -1;
        if (!data) return opus_decode_float(_dec, nullptr, 0, pcm, FRAME_SAMPLES < maxSamples ? FRAME_SAMPLES : maxSamples, 0);
        return opus_decode_float(_dec, data, opus_int32(len), pcm, maxSamples, 0);
    }

    int OpusDec::PacketSamples(const uint8_t* data, size_t len)
    {
        return opus_packet_get_nb_samples(data, opus_int32(len), SAMPLE_RATE);
    }
}
