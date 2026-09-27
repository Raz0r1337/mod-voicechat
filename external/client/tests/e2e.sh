#!/usr/bin/env bash
# ============================================================
#  mod-voicechat - end-to-end test: voicecli A -> Murmur -> voicecli B
#  SPDX-License-Identifier: GPL-2.0-or-later
#
#  DE: Voraussetzung: laufender Murmur (Standard 127.0.0.1:64738, opusthreshold=0).
#      Alice sendet 440 Hz, Bob muss >= 1 s sauberen Ton empfangen - ueber UDP
#      und ueber den TCP-Tunnel.
#  EN: Requires a running Murmur (default 127.0.0.1:64738, opusthreshold=0).
#      Alice sends 440 Hz, Bob must receive >= 1 s of clean tone - via UDP and
#      via the TCP tunnel.
#
#  Usage: tests/e2e.sh <path-to-voicecli> [host] [port]
# ============================================================
set -u
CLI=${1:?path to voicecli}
HOST=${2:-127.0.0.1}
PORT=${3:-64738}
RESULT=0

for MODE in udp tcp; do
    EXTRA=""
    [ "$MODE" = "tcp" ] && EXTRA="--tcp"
    "$CLI" --host "$HOST" --port "$PORT" --user "Bob-$MODE" --expect-tone 440 --seconds 8 $EXTRA > "bob-$MODE.log" 2>&1 &
    BOB=$!
    sleep 1
    "$CLI" --host "$HOST" --port "$PORT" --user "Alice-$MODE" --send-tone 440 --seconds 4 $EXTRA > "alice-$MODE.log" 2>&1
    if wait $BOB; then
        echo "[$MODE] PASS - $(grep -o '[0-9]* clean .*' "bob-$MODE.log")"
    else
        echo "[$MODE] FAIL"; cat "alice-$MODE.log" "bob-$MODE.log"; RESULT=1
    fi
done
exit $RESULT
