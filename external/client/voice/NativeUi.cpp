/*
 * mod-voicechat - Blizzard voice UI integration (see NativeUi.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "NativeUi.h"
#include "Config.h"
#include "LuaLoopback.h"
#include "WowApi.h"

#include <cstdlib>
#include <cstring>

namespace voice
{
    namespace
    {
        // DE: Einmal pro Lua-Zustand (nach /reload neu). Laeuft als sicherer Code (taint 0).
        // EN: once per Lua state (again after /reload). Runs as secure code (taint 0).
        const char* kInstall = R"LUA(
if not MVC then
  MVC = { talking = {}, voice = {}, plates = {} }
  local origTalking, origStatus = UnitIsTalking, GetVoiceStatus
  UnitIsTalking = function(name, ...)
    if name and MVC.talking[name] then return 1 end
    if origTalking then return origTalking(name, ...) end
  end
  GetVoiceStatus = function(unit, ...)
    local n = unit and UnitName(unit)
    if n and MVC.voice[n] then return 1 end
    if origStatus then return origStatus(unit, ...) end
  end
  function MVC.Unit(name)
    if UnitName("player") == name then return "player" end
    for i = 1, 4 do if UnitName("party"..i) == name then return "party"..i end end
    for i = 1, 40 do if UnitName("raid"..i) == name then return "raid"..i end end
  end
  function MVC.Fire(ev, ...)
    local f = EnumerateFrames()
    while f do
      if f:IsEventRegistered(ev) then
        local h = f:GetScript("OnEvent")
        if h then
          local oldThis, oldEvent, oldArg1 = this, event, arg1
          this, event, arg1 = f, ev, (...)
          pcall(h, f, ev, ...)
          this, event, arg1 = oldThis, oldEvent, oldArg1
        end
      end
      f = EnumerateFrames(f)
    end
  end
  -- DE: Voice-Menue zeigt die Geraete von voice.dll (Index 0 = Standard). EN: voice menu shows voice.dll's devices.
  MVC.inDev, MVC.outDev = {}, {}
  local function devName(list, i) i = tonumber(i) or 0 if i == 0 then return DEFAULT or "Default" end return list[i] end
  Sound_ChatSystem_GetNumInputDrivers = function() return #MVC.inDev + 1 end
  Sound_ChatSystem_GetInputDriverNameByIndex = function(i) return devName(MVC.inDev, i) end
  Sound_ChatSystem_GetNumOutputDrivers = function() return #MVC.outDev + 1 end
  Sound_ChatSystem_GetOutputDriverNameByIndex = function(i) return devName(MVC.outDev, i) end
  VoiceEnumerateCaptureDevices = function(i) return devName(MVC.inDev, i) end
  VoiceEnumerateOutputDevices = function(i) return devName(MVC.outDev, i) end
  VoiceSelectCaptureDevice = function() end
  VoiceSelectOutputDevice = function() end
  function MVC.SetDevices(inList, outList)
    local changed = #inList ~= #MVC.inDev or #outList ~= #MVC.outDev
    for i, n in ipairs(inList) do if MVC.inDev[i] ~= n then changed = true end end
    for i, n in ipairs(outList) do if MVC.outDev[i] ~= n then changed = true end end
    MVC.inDev, MVC.outDev = inList, outList
    if changed then
      for _, dd in ipairs({ AudioOptionsVoicePanelInputDeviceDropDown, AudioOptionsVoicePanelOutputDeviceDropDown }) do
        if dd and dd.RefreshValue then pcall(dd.RefreshValue, dd) end
      end
    end
  end
  function MVC.Sync(talking, voice, plates)
    plates = plates or {}
    local changed = false
    for n in pairs(voice) do if not MVC.voice[n] then changed = true end end
    for n in pairs(MVC.voice) do if not voice[n] then changed = true end end
    MVC.voice = voice
    if changed then MVC.Fire("VOICE_STATUS_UPDATE") end
    for n in pairs(talking) do
      if not MVC.talking[n] then
        MVC.talking[n] = 1
        local u = MVC.Unit(n)
        if u then MVC.Fire("VOICE_START", u) end
      end
    end
    for n in pairs(MVC.talking) do
      if not talking[n] then
        MVC.talking[n] = nil
        local u = MVC.Unit(n)
        if u then MVC.Fire("VOICE_STOP", u) end
      end
    end
    -- DE: Sprecherliste oben links (alle hoerbaren Sprecher ausser mir). EN: talker list (all audible speakers but me).
    for n in pairs(plates) do
      if not MVC.plates[n] then MVC.plates[n] = 1 MVC.Fire("VOICE_PLATE_START", n, MVC.Unit(n)) end
    end
    for n in pairs(MVC.plates) do
      if not plates[n] then MVC.plates[n] = nil MVC.Fire("VOICE_PLATE_STOP", n, MVC.Unit(n)) end
    end
  end
end
if IsVoiceChatAllowedByServer() and AudioOptionsFrame and AudioOptionsVoicePanel
   and not AudioOptionsVoicePanel:IsEventRegistered("PLAYER_ENTERING_WORLD") then
  local found = false
  for _, p in ipairs(AudioOptionsFrame.categoryList or {}) do if p == AudioOptionsVoicePanel then found = true end end
  if not found then
    OptionsFrame_AddCategory(AudioOptionsFrame, AudioOptionsVoicePanel)
    BlizzardOptionsPanel_OnEvent(AudioOptionsVoicePanel, "PLAYER_ENTERING_WORLD")
  end
end
)LUA";

        // DE: Lua-String-Literal (wie %q). EN: Lua string literal (like %q).
        std::string LuaQuote(const std::string& s)
        {
            std::string o = "\"";
            for (unsigned char c : s)
            {
                if (c == '"' || c == '\\') { o += '\\'; o += char(c); }
                else if (c < 32 || c == 127) { o += '\\'; o += std::to_string(int(c)); }
                else o += char(c);
            }
            return o + "\"";
        }

        std::string LuaList(const std::vector<std::string>& names)
        {
            std::string o = "{";
            for (const auto& n : names) o += LuaQuote(n) + ",";
            return o + "}";
        }

        std::string LuaSet(const std::set<std::string>& names)
        {
            std::string o = "{";
            for (const auto& n : names) o += "[" + LuaQuote(n) + "]=1,";
            return o + "}";
        }

        struct CVarCtx
        {
            char enable[8], mic[8], mode[8], ptt[64], outVol[16], inVol[16], sens[16], inDev[8], outDev[8];
            char duckSfx[16], duckMusic[16], duckAmb[16];
            bool ok;
        };

        void ReadCVars(void* p)
        {
            auto* c = static_cast<CVarCtx*>(p);
            c->ok = wow::GetCVar("EnableVoiceChat", c->enable, sizeof(c->enable));
            wow::GetCVar("EnableMicrophone", c->mic, sizeof(c->mic));
            wow::GetCVar("VoiceChatMode", c->mode, sizeof(c->mode));
            wow::GetCVar("PushToTalkButton", c->ptt, sizeof(c->ptt));
            wow::GetCVar("OutboundChatVolume", c->outVol, sizeof(c->outVol));
            wow::GetCVar("InboundChatVolume", c->inVol, sizeof(c->inVol));
            wow::GetCVar("VoiceActivationSensitivity", c->sens, sizeof(c->sens));
            wow::GetCVar("Sound_VoiceChatInputDriverIndex", c->inDev, sizeof(c->inDev));
            wow::GetCVar("Sound_VoiceChatOutputDriverIndex", c->outDev, sizeof(c->outDev));
            wow::GetCVar("ChatSoundVolume", c->duckSfx, sizeof(c->duckSfx));
            wow::GetCVar("ChatMusicVolume", c->duckMusic, sizeof(c->duckMusic));
            wow::GetCVar("ChatAmbienceVolume", c->duckAmb, sizeof(c->duckAmb));
        }

        void RunLua(void* p) { wow::LuaExecute(static_cast<const char*>(p)); }
        void AllowVoice(void*) { wow::SetServerVoiceAllowed(); }

        float ToFloat(const char* s, float def, float lo, float hi)
        {
            if (!s[0]) return def;
            float v = float(std::atof(s));
            return v < lo ? lo : v > hi ? hi : v;
        }
    }

    void NativeUi::SetActive(bool active)
    {
        std::lock_guard<std::mutex> g(_mutex);
        if (active && !_active) _dirty = true;
        _active = active;
    }

    void NativeUi::SetState(const std::set<std::string>& talking, const std::set<std::string>& voice,
                            const std::set<std::string>& plates)
    {
        std::lock_guard<std::mutex> g(_mutex);
        if (talking == _talking && voice == _voice && plates == _plates) return;
        _talking = talking;
        _voice = voice;
        _plates = plates;
        _dirty = true;
    }

    void NativeUi::SetDevices(const std::vector<std::string>& capture, const std::vector<std::string>& playback)
    {
        std::lock_guard<std::mutex> g(_mutex);
        if (capture == _capture && playback == _playback) return;
        _capture = capture;
        _playback = playback;
        _dirty = true;
    }

    WowVoiceSettings NativeUi::Settings()
    {
        std::lock_guard<std::mutex> g(_mutex);
        return _settings;
    }

    void NativeUi::MainTick(bool inWorld)
    {
        if (!inWorld)
        {
            // DE: Beim naechsten Login setzt der Server das Flag zurueck, der Lua-Zustand ist neu.
            // EN: on the next login the server resets the flag, the Lua state is new.
            _flagSet = false;
            _nextInstall = 0;
            _wasActive = false;
            return;
        }
        if (_broken) return;

        bool active, dirty;
        std::set<std::string> talking, voice, plates;
        std::vector<std::string> capture, playback;
        {
            std::lock_guard<std::mutex> g(_mutex);
            active = _active;
            dirty = active && _dirty;
            if (active) { talking = _talking; voice = _voice; plates = _plates; capture = _capture; playback = _playback; _dirty = false; }
        }
        if (!active)
        {
            // DE: Beim Abschalten Symbole einmal zuruecksetzen. EN: reset the icons once when switching off.
            if (_wasActive)
            {
                _wasActive = false;
                static const char* reset = "if MVC then MVC.Sync({}, {}, {}) end";
                if (!wow::Guarded(&RunLua, const_cast<char*>(reset)))
                    _broken = true;
            }
            return;
        }
        _wasActive = true;

        unsigned long long now = GetTickCount64();
        if (now >= _nextCVars)
        {
            _nextCVars = now + 250;
            CVarCtx c{};
            if (!wow::Guarded(&ReadCVars, &c))
            {
                _broken = true;
                Log("NativeUi: access violation while reading CVars - Blizzard UI integration disabled");
                return;
            }
            if (c.ok)
            {
                WowVoiceSettings s;
                s.valid = true;
                s.enabled = std::atoi(c.enable) != 0;
                s.microphone = !c.mic[0] || std::atoi(c.mic) != 0;
                s.voiceActivation = std::atoi(c.mode) == 1;
                s.pushToTalk = c.ptt;
                s.inputGain = ToFloat(c.outVol, 1.0f, 0.25f, 2.5f);
                s.outputVolume = ToFloat(c.inVol, 1.0f, 0.0f, 1.0f);
                s.vadSensitivity = ToFloat(c.sens, 0.5f, 0.0f, 1.0f);
                s.inputDevice = std::atoi(c.inDev);
                s.outputDevice = std::atoi(c.outDev);
                s.duckSound = ToFloat(c.duckSfx, 1.0f, 0.0f, 1.0f);
                s.duckMusic = ToFloat(c.duckMusic, 1.0f, 0.0f, 1.0f);
                s.duckAmbience = ToFloat(c.duckAmb, 1.0f, 0.0f, 1.0f);
                std::lock_guard<std::mutex> g(_mutex);
                _settings = s;
            }
        }

        if (!_flagSet)
        {
            if (!wow::Guarded(&AllowVoice, nullptr))
            {
                _broken = true;
                Log("NativeUi: access violation while unlocking the voice options - Blizzard UI integration disabled");
                return;
            }
            _flagSet = true;
            _nextInstall = 0;
            if (!_logged) { Log("NativeUi: Blizzard voice options unlocked"); _logged = true; }
        }

        if (dirty || now >= _nextInstall)
        {
            _nextInstall = now + 5000;
            std::string code = std::string(kInstall) + "\nMVC.SetDevices(" + LuaList(capture) + "," + LuaList(playback) +
                               ")\nMVC.Sync(" + LuaSet(talking) + "," + LuaSet(voice) + "," + LuaSet(plates) + ")\n";
            // DE: Mikrofontest-Funktionen (nach /reload neu registrieren). EN: microphone test functions (re-register after /reload).
            if (!wow::Guarded(&RegisterLoopbackFunctions, nullptr))
            {
                _broken = true;
                Log("NativeUi: access violation in FrameScript_RegisterFunction - Blizzard UI integration disabled");
                return;
            }
            if (!wow::Guarded(&RunLua, const_cast<char*>(code.c_str())))
            {
                _broken = true;
                Log("NativeUi: access violation in FrameScript_Execute - Blizzard UI integration disabled");
            }
        }
    }
}
