/*
 * mod-voicechat - main-thread tick via window subclassing (see GameThread.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "GameThread.h"
#include "Config.h"

namespace voice
{
    namespace
    {
        constexpr UINT WM_VOICE_TICK = WM_APP + 0x5643;   // 'VC'
        GameThread* g_instance = nullptr;

        // DE: Nur POD, damit eine Zugriffsverletzung (Guarded) nichts halb Zerstoertes hinterlaesst.
        // EN: POD only so that an access violation (Guarded) leaves nothing half-destroyed behind.
        struct RawGame
        {
            uint64_t guid;
            bool inWorld;
            wow::C3Vector pos;
            float facing;
            wow::C3Vector camera;
            bool cameraValid;
            uint32_t mapId;
            char name[49];
        };

        void ReadGame(void* p)
        {
            RawGame& r = *static_cast<RawGame*>(p);
            r.guid = wow::ActivePlayerGuid();
            r.inWorld = r.guid != 0 && wow::PlayerPosition(r.pos, r.facing);
            if (!r.inWorld) return;
            r.cameraValid = wow::CameraPosition(r.camera);
            r.mapId = wow::CurrentMapId();
            wow::SafeRead(wow::addr::PlayerName, r.name, 48);
            r.name[48] = 0;
        }
    }

    bool GameThread::EnsureAttached()
    {
        HWND h = wow::MainWindow();
        if (h == _hwnd && _orig) return true;
        if (!h) return false;

        // DE: Fenster neu (z. B. nach Grafik-Neustart) -> altes Fenster zuruecksetzen, neu einhaengen.
        // EN: window is new (e.g. after a graphics restart) -> restore the old window, hook again.
        if (_hwnd && _orig && IsWindow(_hwnd) &&
            GetWindowLongPtrW(_hwnd, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(&GameThread::WndProc))
        {
            if (_unicode) SetWindowLongPtrW(_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(_orig));
            else SetWindowLongPtrA(_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(_orig));
        }
        _hwnd = nullptr;   // DE: _orig bleibt bis zum neuen Wert gueltig. EN: _orig stays valid until replaced.
        g_instance = this;
        _unicode = IsWindowUnicode(h) != FALSE;
        LONG_PTR prev = _unicode
            ? SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&GameThread::WndProc))
            : SetWindowLongPtrA(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&GameThread::WndProc));
        if (!prev) { Log("GameThread: subclassing failed"); return false; }
        _orig = reinterpret_cast<WNDPROC>(prev);
        _hwnd = h;
        _pending = false;
        Log("GameThread: attached to WoW window");
        return true;
    }

    void GameThread::RequestTick()
    {
        // DE: Nur eine Nachricht gleichzeitig (Ladebildschirm blockiert den Hauptthread); nach 2 s ohne
        //     Antwort erneut senden, falls die Nachricht verloren ging.
        // EN: only one message at a time (loading screens block the main thread); re-post after 2 s
        //     without answer in case the message got lost.
        if (!_hwnd) return;
        unsigned long long now = GetTickCount64();
        if (_pending && now - _postedAt < 2000) return;
        _pending = true;
        _postedAt = now;
        if (!PostMessageW(_hwnd, WM_VOICE_TICK, 0, 0)) _pending = false;
    }

    GameSnapshot GameThread::Get() const
    {
        std::lock_guard<std::mutex> g(_mutex);
        return _snap;
    }

    LRESULT CALLBACK GameThread::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        GameThread* self = g_instance;
        if (msg == WM_VOICE_TICK && self)
        {
            self->_pending = false;
            self->Tick();
            return 0;
        }
        WNDPROC orig = self ? self->_orig : nullptr;
        if (!orig) return DefWindowProcW(h, msg, wp, lp);
        return self->_unicode ? CallWindowProcW(orig, h, msg, wp, lp) : CallWindowProcA(orig, h, msg, wp, lp);
    }

    void GameThread::Tick()
    {
        // DE: Hauptthread - WoW-Funktionen sind hier erlaubt. EN: main thread - WoW functions are allowed here.
        GameSnapshot s;
        s.tickMs = GetTickCount64();
        RawGame r{};
        if (!_broken && !wow::Guarded(&ReadGame, &r))
        {
            _broken = true;
            Log("GameThread: access violation while reading game data - addresses in WowApi.h do not match "
                "this Wow.exe; game integration disabled (voice.ini: Mode=standalone, AutoConnectInWorld=0 still works)");
        }
        if (!_broken && r.inWorld)
        {
            s.guid = r.guid;
            s.inWorld = true;
            s.pos = r.pos;
            s.facing = r.facing;
            s.camera = r.camera;
            s.cameraValid = r.cameraValid;
            s.mapId = r.mapId;
            s.name = r.name;
        }
        {
            std::lock_guard<std::mutex> g(_mutex);
            _snap = s;
        }
        if (onTick) onTick(s);
    }
}
