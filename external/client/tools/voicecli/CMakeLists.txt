# mod-voicechat - voicecli (headless test client)
# SPDX-License-Identifier: GPL-2.0-or-later
add_executable(voicecli main.cpp)
target_link_libraries(voicecli PRIVATE voicecore)
