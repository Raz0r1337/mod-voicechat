#!/usr/bin/env bash
# ============================================================
#  mod-voicechat - bot end-to-end test against a real Murmur
#  SPDX-License-Identifier: GPL-2.0-or-later
#
#  DE: Voraussetzung: Murmur mit SuperUser-Passwort (mumble-server -ini ... -supw <pw>).
#      1) Alice + Bob gebunden, gleicher Channel      -> Bob hoert Alice
#      2) Carol + Dave gebunden, verschiedene Channels -> Dave bekommt kein einziges Paket
#      3) Eve ohne Bindung                             -> wird gekickt
#  EN: Requires Murmur with a SuperUser password (mumble-server -ini ... -supw <pw>).
#      1) Alice + Bob bound, same channel         -> Bob hears Alice
#      2) Carol + Dave bound, different channels  -> Dave receives not a single packet
#      3) Eve without binding                     -> gets kicked
#
#  Usage: bot_e2e.sh <bot_test> <voicecli> <superuser-password> [host] [port]
# ============================================================
set -u
BOT=${1:?bot_test}; CLI=${2:?voicecli}; PW=${3:?password}; HOST=${4:-127.0.0.1}; PORT=${5:-64738}
RESULT=0
C="--host $HOST --port $PORT"

"$BOT" $C --password "$PW" --verbose --seconds 40 --kick-unbound 6 \
    --assign Alice=Realm/Map-0 --assign Bob=Realm/Map-0 \
    --assign Carol=Realm/Map-0 --assign Dave=Realm/Map-1/Inst-5 > bot.log 2>&1 &
BOTPID=$!
sleep 3

check() { if [ "$1" -eq 0 ]; then echo "[$2] PASS"; else echo "[$2] FAIL"; RESULT=1; fi; }

# 1) gleicher Channel / same channel
"$CLI" $C --user Bob --bind 0123456789abcdef01 --expect-tone 440 --seconds 9 > bob.log 2>&1 & P1=$!
sleep 1
"$CLI" $C --user Alice --bind 0123456789abcdef02 --send-tone 440 --seconds 4 > alice.log 2>&1
wait $P1; check $? "same channel: $(grep -o 'PASS.*\|FAIL.*' bob.log | tail -1)"

# 2) verschiedene Channels / different channels
"$CLI" $C --user Dave --bind 0123456789abcdef04 --expect-nothing --seconds 9 > dave.log 2>&1 & P2=$!
sleep 1
"$CLI" $C --user Carol --bind 0123456789abcdef03 --send-tone 440 --seconds 4 > carol.log 2>&1
wait $P2; check $? "different channels: $(grep -o 'PASS.*\|FAIL.*' dave.log | tail -1)"

# 3) ohne Bindung -> Kick / without binding -> kick
"$CLI" $C --user Eve --seconds 10 > eve.log 2>&1
grep -q "removed from server" eve.log; check $? "unbound user kicked"

wait $BOTPID; check $? "bot ran without errors"
grep -q "permission denied" bot.log && { echo "[acl] FAIL: permission denied"; RESULT=1; }
# DE: Wurde jeder in den richtigen Channel verschoben? EN: was everyone moved into the right channel?
for pair in bob:Map-0 alice:Map-0 carol:Map-0 dave:Inst-5; do
    grep -q "moved to channel '${pair#*:}'" "${pair%%:*}.log"; check $? "${pair%%:*} moved to ${pair#*:}"
done
[ $RESULT -ne 0 ] && { echo "--- bot.log"; cat bot.log; }
exit $RESULT
