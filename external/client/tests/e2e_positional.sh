#!/usr/bin/env bash
# ============================================================
#  mod-voicechat - positional end-to-end test through a real Murmur
#  SPDX-License-Identifier: GPL-2.0-or-later
#
#  DE: Bob hoert an (0,0,0) mit Blick nach Norden, Alice spricht (440 Hz).
#      1) Alice 5 yd entfernt, gleicher Kontext  -> Ton hoerbar
#      2) Alice 100 yd entfernt, gleicher Kontext -> Pakete kommen an, aber stumm
#      3) Alice 5 yd entfernt, anderer Kontext (andere Map) -> Murmur entfernt die
#         Position -> stumm (Map-Trennung)
#  EN: Bob listens at (0,0,0) facing north, Alice speaks (440 Hz).
#      1) Alice 5 yd away, same context  -> tone audible
#      2) Alice 100 yd away, same context -> packets arrive, but silent
#      3) Alice 5 yd away, different context (other map) -> Murmur strips the
#         position -> silent (map isolation)
#
#  Usage: tests/e2e_positional.sh <path-to-voicecli> [host] [port]
# ============================================================
set -u
CLI=${1:?path to voicecli}
HOST=${2:-127.0.0.1}
PORT=${3:-64738}
RESULT=0

run_case() {   # name, alice-pos, alice-context, bob-expectation
    local NAME=$1 APOS=$2 ACTX=$3 EXPECT=$4
    "$CLI" --host "$HOST" --port "$PORT" --user "Bob-$NAME" --listen-pos 0,0,0 --context "wow335|0" \
        $EXPECT --seconds 8 > "bob-$NAME.log" 2>&1 &
    local BOB=$!
    sleep 1
    "$CLI" --host "$HOST" --port "$PORT" --user "Alice-$NAME" --pos "$APOS" --context "$ACTX" \
        --send-tone 440 --seconds 4 > "alice-$NAME.log" 2>&1
    if wait $BOB; then
        echo "[$NAME] PASS - $(grep -o -E '(PASS|FAIL): .*' "bob-$NAME.log")"
    else
        echo "[$NAME] FAIL"; cat "alice-$NAME.log" "bob-$NAME.log"; RESULT=1
    fi
}

run_case near     5,0,0   "wow335|0" "--expect-tone 440"
run_case far      100,0,0 "wow335|0" "--expect-silence"
run_case othermap 5,0,0   "wow335|1" "--expect-silence"
exit $RESULT
