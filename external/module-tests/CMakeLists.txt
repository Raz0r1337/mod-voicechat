# ============================================================
#  mod-voicechat - standalone tests for AzerothCore-independent module parts
#  SPDX-License-Identifier: GPL-2.0-or-later
#
#  DE: Baut den MumbleBot aus src/ ohne AzerothCore (Boost + OpenSSL vom System).
#  EN: Builds the MumbleBot from src/ without AzerothCore (Boost + OpenSSL from the system).
# ============================================================
cmake_minimum_required(VERSION 3.16)
project(mod_voicechat_module_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

set(MODULE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_executable(bot_test bot_test.cpp ${MODULE_SRC}/MumbleBot.cpp)
target_include_directories(bot_test PRIVATE ${MODULE_SRC} ${MODULE_SRC}/shared)
target_link_libraries(bot_test PRIVATE Boost::boost OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_compile_options(bot_test PRIVATE -Wall -Wextra)
