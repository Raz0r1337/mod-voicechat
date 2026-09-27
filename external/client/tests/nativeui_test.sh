#!/usr/bin/env bash
# mod-voicechat - extracts the Lua bridge from voice/NativeUi.cpp and tests it with Lua 5.1
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
DIR=$(cd "$(dirname "$0")" && pwd)
LUA=${LUA:-lua5.1}
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
sed -n '/R"LUA(/,/)LUA"/p' "$DIR/../voice/NativeUi.cpp" | sed '1d;$d' > "$TMP"
[ -s "$TMP" ] || { echo "FAIL: Lua bridge not found in NativeUi.cpp"; exit 1; }
"$LUA" "$DIR/nativeui_test.lua" "$TMP"
