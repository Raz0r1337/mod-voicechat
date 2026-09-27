# mod-voicechat - voicecore (Mumble client stack + Opus + mixer)
# SPDX-License-Identifier: GPL-2.0-or-later
add_library(voicecore STATIC
    src/OcbCrypt.cpp
    src/Net.cpp
    src/MumbleClient.cpp
    src/OpusCodec.cpp
    src/AudioMixer.cpp
    src/Transmitter.cpp
    src/Spatial.cpp)

target_include_directories(voicecore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/shared)   # MumbleProtocol.h (gemeinsam / shared)

target_link_libraries(voicecore PUBLIC opus mbedtls mbedx509 mbedcrypto)

find_package(Threads REQUIRED)
target_link_libraries(voicecore PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(voicecore PUBLIC ws2_32 bcrypt)
    target_compile_definitions(voicecore PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
endif()

if(MSVC)
    target_compile_options(voicecore PRIVATE /W4)
else()
    target_compile_options(voicecore PRIVATE -Wall -Wextra)
endif()
