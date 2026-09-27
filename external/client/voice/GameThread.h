/*
 * mod-voicechat - runs code on the WoW main thread without code patches
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Das WoW-Fenster wird per SetWindowLongPtr "gesubclassed". Der Worker
 *     schickt regelmaessig eine eigene Fensternachricht; WoW verarbeitet sie
 *     in seiner normalen Nachrichtenschleife -> unser Tick laeuft im
 *     Hauptthread, zwischen zwei Frames. Keine Code-Patches noetig.
 * EN: The WoW window is subclassed via SetWindowLongPtr. The worker posts a
 *     custom window message regularly; WoW processes it in its normal message
 *     loop -> our tick runs on the main thread, between two frames. No code
 *     patches needed.
 */
#pragma once

#include "WowApi.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace voice
{
    struct GameSnapshot
    {
        bool inWorld = false;
        uint64_t guid = 0;
        wow::C3Vector pos;
        float facing = 0.0f;
        wow::C3Vector camera;
        bool cameraValid = false;
        uint32_t mapId = 0;
        std::string name;
        unsigned long long tickMs = 0;   // letzter Tick / last tick (GetTickCount64)
    };

    class GameThread
    {
    public:
        // DE: Vom Worker aufrufen. EN: call from the worker.
        bool EnsureAttached();
        void RequestTick();
        GameSnapshot Get() const;

        // DE: Zusaetzliche Arbeit im Hauptthread (MVCP, Occlusion ...). EN: extra main-thread work.
        std::function<void(const GameSnapshot&)> onTick;

    private:
        static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
        void Tick();

        HWND _hwnd = nullptr;
        WNDPROC _orig = nullptr;
        bool _unicode = false;
        std::atomic<bool> _pending{ false };
        mutable std::mutex _mutex;
        GameSnapshot _snap;
    };
}
