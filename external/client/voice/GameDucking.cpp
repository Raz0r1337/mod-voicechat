/*
 * mod-voicechat - lower game sounds while voice is active (see GameDucking.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "GameDucking.h"
#include "Config.h"
#include "WowApi.h"

#include <cstdio>
#include <cstdlib>

namespace voice
{
    namespace
    {
        const char* const kNames[voicecore::Ducking::CHANNELS] = { "Sound_SFXVolume", "Sound_MusicVolume", "Sound_AmbienceVolume" };

        struct GetCtx { const char* name; char buf[32]; bool ok; };
        void DoGet(void* p) { auto* c = static_cast<GetCtx*>(p); c->ok = wow::GetCVar(c->name, c->buf, sizeof(c->buf)); }
        struct SetCtx { const char* name; const char* value; bool ok; };
        void DoSet(void* p) { auto* c = static_cast<SetCtx*>(p); c->ok = wow::SetCVar(c->name, c->value); }
    }

    GameDucking::GameDucking() : _core(Config::ModuleDir() + "\\voice.duck") { }

    void GameDucking::SetTarget(bool duck, float sfx, float music, float ambience)
    {
        const float f[voicecore::Ducking::CHANNELS] = { sfx, music, ambience };
        _core.SetTarget(duck, f);
    }

    void GameDucking::MainTick(bool inWorld)
    {
        if (_broken) return;
        auto read = [this](int i, float& v) {
            GetCtx c{ kNames[i], {}, false };
            if (_broken || !wow::Guarded(&DoGet, &c)) { _broken = true; return false; }
            if (!c.ok || !c.buf[0]) return false;
            v = float(std::atof(c.buf));
            return true;
        };
        auto write = [this](int i, float v) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.3f", double(v));
            SetCtx c{ kNames[i], buf, false };
            if (_broken || !wow::Guarded(&DoSet, &c)) { _broken = true; return false; }
            return c.ok;
        };
        _core.Tick(inWorld, GetTickCount64(), read, write);
        if (_broken)
            Log("ducking: access violation while touching the sound CVars - disabled (voice.duck restores next start)");
        else if (_core.RestoredAfterCrash() && !_loggedRestore)
        {
            _loggedRestore = true;
            Log("ducking: restored game volumes after an unexpected exit");
        }
    }
}
