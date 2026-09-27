# ============================================================
#  mod-voicechat - client side (voicecore, voicecli, voice.dll)
#  SPDX-License-Identifier: GPL-2.0-or-later
#
#  DE: Wird NICHT von AzerothCore gebaut. Eigenstaendiges CMake-Projekt.
#      Windows (32 Bit!):  cmake -A Win32 ..   bzw. MinGW: -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw32.cmake
#      Linux (Tests):      cmake ..
#  EN: NOT built by AzerothCore. Stand-alone CMake project.
#      Windows (32-bit!):  cmake -A Win32 ..   or MinGW: -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw32.cmake
#      Linux (tests):      cmake ..
# ============================================================
cmake_minimum_required(VERSION 3.16)
project(mod_voicechat_client C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Wow.exe 3.3.5a ist 32 Bit - bitte mit -A Win32 bauen / Wow.exe 3.3.5a is 32-bit - build with -A Win32")
endif()

option(VC_BUILD_CLI "Build voicecli test tool" ON)
option(VC_BUILD_TESTS "Build unit tests" ON)
option(VC_BUILD_DLL "Build voice.dll (Windows only)" ${WIN32})

if(MSVC)
    # DE: statische Laufzeit -> keine VC-Redist beim Spieler. Die Policy-Vorgabe sorgt dafuer,
    #     dass auch Opus/mbedTLS (alte cmake_minimum_required) mit /MT gebaut werden.
    # EN: static runtime -> no VC redist needed. The policy default makes sure that
    #     Opus/mbedTLS (old cmake_minimum_required) are built with /MT as well.
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ------------------------------------------------------------
#  Abhaengigkeiten / dependencies (BSD / Apache-2.0-or-GPL-2.0+ / MIT-0)
# ------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG        v1.5.2
    GIT_SHALLOW    TRUE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_STATIC_RUNTIME ON CACHE BOOL "" FORCE)      # MSVC /MT (Opus setzt die Laufzeit selbst / sets the runtime itself)

FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.2/mbedtls-3.6.2.tar.bz2
    URL_HASH SHA256=8b54fb9bcf4d5a7078028e0520acddefb7900b3e66fec7f7175bb5b7d85ccdca
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(opus mbedtls)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)   # nur Header / header only
endif()

add_subdirectory(voicecore)

if(VC_BUILD_CLI)
    add_subdirectory(tools/voicecli)
endif()

if(VC_BUILD_TESTS)
    enable_testing()
    add_executable(spatial_test tests/spatial_test.cpp)
    target_link_libraries(spatial_test PRIVATE voicecore)
    add_test(NAME spatial COMMAND spatial_test)
    add_executable(occlusion_test tests/occlusion_test.cpp)
    target_link_libraries(occlusion_test PRIVATE voicecore)
    add_test(NAME occlusion COMMAND occlusion_test)
    add_executable(ducking_test tests/ducking_test.cpp)
    target_link_libraries(ducking_test PRIVATE voicecore)
    add_test(NAME ducking COMMAND ducking_test)
endif()

if(VC_BUILD_DLL)
    if(NOT WIN32)
        message(FATAL_ERROR "voice.dll kann nur fuer Windows gebaut werden / voice.dll can only be built for Windows")
    endif()
    add_subdirectory(voice)
endif()
