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
    }

    bool GameThread::EnsureAttached()
    {
        HWND h = wow::MainWindow();
        if (h == _hwnd && _orig) return true;
        if (!h) return false;

        // DE: Fenster neu (z. B. nach Grafik-Neustart) -> neu einhaengen.
        // EN: window is new (e.g. after a graphics restart) -> hook again.
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
        // DE: Nur eine Nachricht gleichzeitig (Ladebildschirm blockiert den Hauptthread).
        // EN: only one message at a time (loading screens block the main thread).
        if (!_hwnd || _pending.exchange(true)) return;
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
        s.guid = wow::ActivePlayerGuid();
        s.inWorld = s.guid != 0 && wow::PlayerPosition(s.pos, s.facing);
        if (s.inWorld)
        {
            s.cameraValid = wow::CameraPosition(s.camera);
            s.mapId = wow::CurrentMapId();
            s.name = wow::PlayerName();
        }
        {
            std::lock_guard<std::mutex> g(_mutex);
            _snap = s;
        }
        if (onTick) onTick(s);
    }
}
