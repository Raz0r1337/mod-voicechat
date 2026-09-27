@echo off
REM mod-voicechat - voice.dll loader patcher (Wow.exe 3.3.5a)
REM DE: Wow.exe in diesen Ordner legen oder Batch im WoW-Ordner starten.
REM EN: Put Wow.exe into this folder or run the batch inside the WoW folder.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0voice_patcher.ps1" %*
pause
