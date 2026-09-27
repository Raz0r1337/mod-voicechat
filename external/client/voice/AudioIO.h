/*
 * mod-voicechat - audio devices via miniaudio (WASAPI)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace voice
{
    class AudioIO
    {
    public:
        AudioIO();
        ~AudioIO();

        // DE: capture: 48 kHz mono; playback: 48 kHz stereo interleaved.
        // EN: capture: 48 kHz mono; playback: 48 kHz stereo interleaved.
        std::function<void(const float* mono, size_t frames)> onCapture;
        std::function<void(float* stereo, size_t frames)> onPlayback;

        bool Start(const std::string& inputDevice, const std::string& outputDevice);
        void Stop();
        bool Running() const;

        // DE: Geraetenamen (UTF-8) fuer das Voice-Menue. EN: device names (UTF-8) for the voice menu.
        static bool ListDevices(std::vector<std::string>& capture, std::vector<std::string>& playback);

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}
