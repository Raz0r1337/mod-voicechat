/*
 * mod-voicechat - audio devices via miniaudio (see AudioIO.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include "miniaudio.h"

#include "AudioIO.h"
#include "Config.h"

#include <cstring>

namespace voice
{
    struct AudioIO::Impl
    {
        ma_context ctx{};
        ma_device capture{};
        ma_device playback{};
        bool ctxOk = false, capOk = false, playOk = false;
        AudioIO* owner = nullptr;
    };

    AudioIO::AudioIO() = default;
    AudioIO::~AudioIO() { Stop(); }

    namespace
    {
        void OnCapture(ma_device* dev, void*, const void* input, ma_uint32 frames)
        {
            auto* self = static_cast<AudioIO*>(dev->pUserData);
            if (self->onCapture && input) self->onCapture(static_cast<const float*>(input), frames);
        }

        void OnPlayback(ma_device* dev, void* output, const void*, ma_uint32 frames)
        {
            auto* self = static_cast<AudioIO*>(dev->pUserData);
            if (self->onPlayback) self->onPlayback(static_cast<float*>(output), frames);
            else std::memset(output, 0, frames * 2 * sizeof(float));
        }

        // DE: Geraet per Namens-Teilstring suchen. EN: find a device by name substring.
        const ma_device_id* FindDevice(ma_context* ctx, ma_device_type type, const std::string& name, ma_device_id& out)
        {
            if (name.empty()) return nullptr;
            ma_device_info *play = nullptr, *cap = nullptr;
            ma_uint32 nPlay = 0, nCap = 0;
            if (ma_context_get_devices(ctx, &play, &nPlay, &cap, &nCap) != MA_SUCCESS) return nullptr;
            ma_device_info* list = type == ma_device_type_capture ? cap : play;
            ma_uint32 n = type == ma_device_type_capture ? nCap : nPlay;
            for (ma_uint32 i = 0; i < n; ++i)
                if (std::strstr(list[i].name, name.c_str())) { out = list[i].id; return &out; }
            Log("audio device not found: " + name);
            return nullptr;
        }
    }

    bool AudioIO::Start(const std::string& inputDevice, const std::string& outputDevice)
    {
        Stop();
        _impl = std::make_unique<Impl>();
        Impl& d = *_impl;
        if (ma_context_init(nullptr, 0, nullptr, &d.ctx) != MA_SUCCESS) { Log("audio: context init failed"); return false; }
        d.ctxOk = true;

        ma_device_id inId, outId;
        ma_device_config cc = ma_device_config_init(ma_device_type_capture);
        cc.capture.pDeviceID = FindDevice(&d.ctx, ma_device_type_capture, inputDevice, inId);
        cc.capture.format = ma_format_f32;
        cc.capture.channels = 1;
        cc.sampleRate = 48000;
        cc.periodSizeInMilliseconds = 10;
        cc.dataCallback = OnCapture;
        cc.pUserData = this;
        if (ma_device_init(&d.ctx, &cc, &d.capture) == MA_SUCCESS)
        {
            d.capOk = true;   // DE: initialisiert -> muss in Stop() freigegeben werden / initialised -> must be freed in Stop()
            if (ma_device_start(&d.capture) != MA_SUCCESS)
            {
                ma_device_uninit(&d.capture);
                d.capOk = false;
            }
        }
        if (!d.capOk)
            Log("audio: microphone could not be opened (listening only)");

        ma_device_config pc = ma_device_config_init(ma_device_type_playback);
        pc.playback.pDeviceID = FindDevice(&d.ctx, ma_device_type_playback, outputDevice, outId);
        pc.playback.format = ma_format_f32;
        pc.playback.channels = 2;
        pc.sampleRate = 48000;
        pc.periodSizeInMilliseconds = 10;
        pc.dataCallback = OnPlayback;
        pc.pUserData = this;
        if (ma_device_init(&d.ctx, &pc, &d.playback) == MA_SUCCESS)
        {
            d.playOk = true;
            if (ma_device_start(&d.playback) != MA_SUCCESS)
            {
                ma_device_uninit(&d.playback);
                d.playOk = false;
            }
        }
        if (!d.playOk)
            Log("audio: output device could not be opened");

        Log(std::string("audio: capture=") + (d.capOk ? d.capture.capture.name : "-") + " playback=" + (d.playOk ? d.playback.playback.name : "-"));
        return d.capOk || d.playOk;
    }

    void AudioIO::Stop()
    {
        if (!_impl) return;
        Impl& d = *_impl;
        if (d.capOk) ma_device_uninit(&d.capture);
        if (d.playOk) ma_device_uninit(&d.playback);
        if (d.ctxOk) ma_context_uninit(&d.ctx);
        _impl.reset();
    }

    bool AudioIO::Running() const { return _impl && (_impl->capOk || _impl->playOk); }
}
