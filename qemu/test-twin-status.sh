#!/usr/bin/env bash
# qemu/test-twin-status.sh — regression test for "a running twin is reused, not re-booted".
#
#   ./qemu/test-twin-status.sh        (needs ./qemu/fetch-qemu.sh first; takes a few minutes)
#
# Exercises run.sh, studio.sh, console.sh, twin-status.sh and verify-lab --boot-twin against
# the real twin on the default console socket. It STOPS any twin/Studio already running on
# that socket at the start and at the end.
set -u
cd "$(dirname "$0")/.."
SOCK=/tmp/plc-serial
PASS=0; FAIL=0
ok()  { echo "  PASS  $*"; PASS=$((PASS + 1)); }
bad() { echo "  FAIL  $*"; FAIL=$((FAIL + 1)); }
check() { if eval "$1"; then ok "$2"; else bad "$2"; fi; }
show() { sed 's/^/        | /' <<<"$1"; }
twins()   { pgrep -f "[q]emu-system-xtensa.*unix:$SOCK,server" | wc -l | tr -d ' '; }
twinpid() { pgrep -f "[q]emu-system-xtensa.*unix:$SOCK,server" | head -n1; }
bridges() { pgrep -f "[t]win-bridge.py --port 8080 --serial $SOCK" | wc -l | tr -d ' '; }
wait_running() { for _ in $(seq 1 200); do ./qemu/twin-status.sh >/dev/null 2>&1 && return 0; sleep 0.2; done; return 1; }
wait_http()    { for _ in $(seq 1 100); do
                   [ "$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:8080/logic_studio.html")" = 200 ] && return 0
                   sleep 0.2; done; return 1; }
console_says() {  # send one command through the Studio bridge's console port, print the reply
  python3 - "$1" <<'PY'
import socket, sys, time
s = socket.create_connection(("127.0.0.1", 8081), timeout=5); s.settimeout(0.3)
s.sendall(sys.argv[1].encode() + b"\n"); out = b""; end = time.time() + 4
while time.time() < end:
    try:
        d = s.recv(4096)
        if d: out += d
    except socket.timeout:
        pass
print(out.decode("utf-8", "replace"))
PY
}

./qemu/twin-status.sh stop >/dev/null 2>&1

echo "1. nothing running"
out=$(./qemu/twin-status.sh 2>&1); rc=$?; show "$out"
check '[ $rc -eq 1 ] && grep -q STOPPED <<<"$out"' "status reports STOPPED (exit 1)"

echo "2. cold boot with run.sh"
setsid nohup ./qemu/run.sh >/tmp/tts-run1.log 2>&1 < /dev/null &
check 'wait_running' "twin boots and reports running"
check '[ "$(twins)" = 1 ]' "exactly one twin process"
PID1=$(twinpid)

echo "3. run.sh again while it is running"
out=$(timeout 40 ./qemu/run.sh 2>&1); rc=$?; show "$out"
check '[ $rc -eq 0 ] && grep -q "already running" <<<"$out"' "second run.sh does not boot (exit 0, says already running)"
check '[ "$(twins)" = 1 ] && [ "$(twinpid)" = "$PID1" ]' "still one twin, same pid $PID1"

echo "4. explicitly asking for a different image while one is running"
cp qemu/twin/stamplc-flash-8MB.bin /tmp/tts-other-8MB.bin
out=$(FLASH_IMG=/tmp/tts-other-8MB.bin timeout 40 ./qemu/run.sh 2>&1); rc=$?; show "$out"
check '[ $rc -eq 3 ] && grep -q "RESTART=1" <<<"$out"' "refused with the RESTART=1 switch command (exit 3)"
check '[ "$(twinpid)" = "$PID1" ]' "running twin untouched"

echo "5. studio.sh with the twin already running"
setsid nohup ./qemu/studio.sh >/tmp/tts-studio1.log 2>&1 < /dev/null &
check 'wait_http' "Studio served on :8080"
show "$(head -4 /tmp/tts-studio1.log)"
check 'grep -q "reusing it" /tmp/tts-studio1.log' "studio.sh reused the twin"
check '[ "$(twins)" = 1 ] && [ "$(twinpid)" = "$PID1" ]' "no second twin booted"
check '[ "$(bridges)" = 1 ]' "one Studio server"

echo "6. studio.sh again"
out=$(timeout 40 ./qemu/studio.sh 2>&1); rc=$?; show "$out"
check '[ $rc -eq 0 ] && grep -q "already running on :8080" <<<"$out"' "second studio.sh starts nothing (exit 0)"
check '[ "$(bridges)" = 1 ] && [ "$(twins)" = 1 ]' "still one server, one twin"

echo "7. studio.sh asking for a different build"
out=$(timeout 40 ./qemu/studio.sh malicious 2>&1); rc=$?; show "$out"
check '[ $rc -eq 3 ] && grep -q "RESTART=1 ./qemu/studio.sh malicious" <<<"$out"' "refused with the switch command (exit 3)"

echo "8. text console alongside the Studio"
out=$(console_says "status"); show "$(head -6 <<<"$out")"
check 'grep -qi "mode" <<<"$out"' "console via the bridge answers 'status'"

echo "9. verify-lab --boot-twin with the twin and Studio already running"
# (P5 is left out here: the student twin image ships without the Part 4/5 motor program until the
#  student loads it from the Studio — step 9b checks that P5 says so; step 13 runs P5 on the factory image.)
out=$(timeout 400 python3 assessment/verify-lab.py --boot-twin P1 P3 P6 P8 2>&1); rc=$?; show "$out"
check 'grep -q "reusing it" <<<"$out"' "verifier reused the running twin"
check 'grep -q "via the Studio bridge" <<<"$out"' "verifier attached through the bridge (not a dead second socket client)"
check '[ $rc -eq 0 ]' "verifier exit 0 (no FAIL)"
check '[ "$(twinpid)" = "$PID1" ]' "verifier left the student's twin running"

echo "9b. P5 on a twin without the motor program explains what to do"
out=$(timeout 120 python3 assessment/verify-lab.py P5 2>&1); show "$out"
check 'grep -q "Logic Studio" <<<"$out" || grep -q "PASS\] P5" <<<"$out"' "P5 passes, or names the missing program and the fix"

echo "10. RESTART=1 reboots; the open Studio server reconnects"
setsid nohup env RESTART=1 ./qemu/run.sh >/tmp/tts-run2.log 2>&1 < /dev/null &
for _ in $(seq 1 100); do p=$(twinpid); [ -n "$p" ] && [ "$p" != "$PID1" ] && break; sleep 0.2; done
check 'wait_running && [ "$(twinpid)" != "$PID1" ]' "new twin pid $(twinpid) (was $PID1)"
check '[ "$(bridges)" = 1 ]' "Studio server kept running"
for _ in $(seq 1 60); do grep -q "reconnect" /tmp/tts-studio1.log && break; sleep 0.5; done
out=""; for _ in 1 2 3 4 5 6; do out=$(console_says "status"); grep -qi "mode" <<<"$out" && break; sleep 3; done
check 'grep -qi "mode" <<<"$out"' "console through the same server reaches the new twin"
PID2=$(twinpid)

echo "11. a crashed twin leaves a stale socket: next boot must not be skipped"
kill -9 "$PID2"; sleep 1
out=$(./qemu/twin-status.sh 2>&1); rc=$?; show "$out"
check '[ -S "$SOCK" ] && [ $rc -eq 1 ] && grep -q stale <<<"$out"' "stale socket detected as STOPPED"
setsid nohup ./qemu/run.sh >/tmp/tts-run3.log 2>&1 < /dev/null &
check 'wait_running' "run.sh boots fresh over the stale socket"

echo "11b. a live twin whose socket file was deleted is reported, and RESTART=1 replaces it"
rm -f "$SOCK"
out=$(timeout 40 ./qemu/run.sh 2>&1); rc=$?; show "$out"
check '[ $rc -eq 3 ] && grep -q "was deleted" <<<"$out"' "run.sh reports the orphaned twin instead of booting a second"
PID3=$(twinpid)
setsid nohup env RESTART=1 ./qemu/run.sh >/tmp/tts-run4.log 2>&1 < /dev/null &
for _ in $(seq 1 100); do p=$(twinpid); [ -n "$p" ] && [ "$p" != "$PID3" ] && break; sleep 0.2; done
check 'wait_running && [ "$(twinpid)" != "$PID3" ] && [ "$(twins)" = 1 ]' "RESTART=1 replaced it with one fresh twin"

echo "12. stop"
out=$(./qemu/twin-status.sh stop 2>&1); show "$out"
check '[ "$(twins)" = 0 ] && [ "$(bridges)" = 0 ] && [ ! -e "$SOCK" ]' "twin, Studio server and socket all gone"

echo "13. CI path: verify-lab --boot-twin with nothing running boots its own and tears it down"
out=$(timeout 400 python3 assessment/verify-lab.py --boot-twin P1 P5 2>&1); rc=$?; show "$out"
check '[ $rc -eq 0 ] && ! grep -q "reusing it" <<<"$out"' "booted its own twin, exit 0"
sleep 1
check '[ "$(twins)" = 0 ]' "and tore it down"

rm -f /tmp/tts-other-8MB.bin
echo
echo "== $PASS passed, $FAIL failed =="
[ "$FAIL" -eq 0 ]
