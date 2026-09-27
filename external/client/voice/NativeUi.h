/*
 * mod-voicechat - Blizzard voice UI integration (phase 7)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Nutzt die originale Voice-Oberflaeche von WoW 3.3.5a:
 *     - Voice-Optionsmenue freischalten (Server-Flag wie beim Login, still: kein Event,
 *       kein Start der alten Blizzard-Voice-Engine) und bei Bedarf nachtraeglich eintragen
 *     - Einstellungen aus den CVars lesen (EnableVoiceChat, EnableMicrophone, VoiceChatMode,
 *       PushToTalkButton, Outbound-/InboundChatVolume, VoiceActivationSensitivity)
 *     - Sprecher-Symbole: UnitIsTalking/GetVoiceStatus erweitern und VOICE_START/VOICE_STOP/
 *       VOICE_STATUS_UPDATE an alle Frames liefern, die sie registriert haben
 *     Alles laeuft im WoW-Hauptthread (MainTick) unter SEH-Schutz; der Worker setzt nur Zustand.
 * EN: Uses WoW 3.3.5a's original voice UI:
 *     - unlock the voice options menu (server flag as at login, silently: no event, no start of
 *       the old Blizzard voice engine) and add it afterwards if needed
 *     - read the settings from the CVars (see above)
 *     - speaker icons: extend UnitIsTalking/GetVoiceStatus and deliver VOICE_START/VOICE_STOP/
 *       VOICE_STATUS_UPDATE to all frames that registered them
 *     Everything runs on the WoW main thread (MainTick) under the SEH guard; the worker only sets state.
 */
#pragma once

#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace voice
{
    struct WowVoiceSettings
    {
        bool valid = false;             // DE: mindestens einmal gelesen / read at least once
        bool enabled = false;           // EnableVoiceChat
        bool microphone = true;         // EnableMicrophone
        bool voiceActivation = false;   // VoiceChatMode == 1
        std::string pushToTalk;         // PushToTalkButton, z. B. / e.g. "LCTRL-F"
        float inputGain = 1.0f;         // OutboundChatVolume (0.25 .. 2.5)
        float outputVolume = 1.0f;      // InboundChatVolume (0 .. 1)
        float vadSensitivity = 0.5f;    // VoiceActivationSensitivity (0 .. 1)
        int inputDevice = 0;            // Sound_VoiceChatInputDriverIndex (0 = Standard / default)
        int outputDevice = 0;           // Sound_VoiceChatOutputDriverIndex
    };

    class NativeUi
    {
    public:
        // DE: Worker. EN: worker.
        void SetActive(bool active);
        void SetState(const std::set<std::string>& talking, const std::set<std::string>& voice);
        // DE: Geraeteliste fuer das Voice-Menue (Index 1..n, 0 = Standard). EN: device list for the voice menu.
        void SetDevices(const std::vector<std::string>& capture, const std::vector<std::string>& playback);
        WowVoiceSettings Settings();

        // DE: Hauptthread. EN: main thread.
        void MainTick(bool inWorld);

    private:
        std::mutex _mutex;
        bool _active = false;
        std::set<std::string> _talking, _voice;
        std::vector<std::string> _capture, _playback;
        bool _dirty = true;
        WowVoiceSettings _settings;

        // DE: nur Hauptthread / main thread only
        bool _broken = false;
        bool _flagSet = false;
        bool _logged = false;
        bool _wasActive = false;
        unsigned long long _nextCVars = 0, _nextInstall = 0;
    };
}
