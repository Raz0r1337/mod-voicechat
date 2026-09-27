# mod-voicechat - voice.dll (loaded by the patched Wow.exe)
# SPDX-License-Identifier: GPL-2.0-or-later
add_library(voice SHARED
    dllmain.cpp
    VoiceApp.cpp
    Config.cpp
    AudioIO.cpp
    WowApi.cpp
    GameThread.cpp)

set_target_properties(voice PROPERTIES PREFIX "" OUTPUT_NAME "voice")
target_include_directories(voice PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(voice PRIVATE voicecore ole32 winmm)

if(MINGW)
    target_link_options(voice PRIVATE -static -Wl,--enable-stdcall-fixup)
endif()
