#!/usr/bin/env bash
# test-live-tooling.sh — acceptance checks for the live-lab tooling, run in the Codespace
# against a RUNNING twin + Studio server. Prints PASS / FAIL / SKIP per check; exit 1 on any FAIL.
#
#   ./qemu/studio.sh                       # terminal 1 (leave it running)
#   ./tools/test-live-tooling.sh           # terminal 2
#   SKIP_SBOM=1 ./tools/test-live-tooling.sh    # skip the slow grype step
#
# What it touches on your twin: it presses IN1 (di0) at the chip during two short bus captures,
# writes 0 to the unassigned aux_coil0 (Modbus coil 8) once to create an audit-log line, sends
# `diag json` and `log` on the console, and restores the input byte at the end.
# Browser checks need Playwright:  npm i playwright && npx playwright install --with-deps chromium
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
PORT="${BRIDGE_PORT:-8088}"; BASE="http://127.0.0.1:$PORT"; CON="${BRIDGE_CONSOLE_PORT:-8081}"
QMP_SOCK="${QMP_SOCK:-/tmp/plc-qmp}"; SERIAL_SOCK="${SERIAL_SOCK:-/tmp/plc-serial}"
NPASS=0; NFAIL=0; NSKIP=0
pass() { printf '  PASS  %s\n' "$*"; NPASS=$((NPASS+1)); }
fail() { printf '  FAIL  %s\n' "$*"; NFAIL=$((NFAIL+1)); }
skip() { printf '  SKIP  %s\n' "$*"; NSKIP=$((NSKIP+1)); }
tally() { while IFS= read -r l; do case "$l" in PASS*) pass "${l#PASS }";; FAIL*) fail "${l#FAIL }";; SKIP*) skip "${l#SKIP }";; *) echo "        $l";; esac; done; }

echo "== live-lab tooling acceptance =="
echo "-- 0. preconditions"
if curl -s -o /dev/null --max-time 3 "$BASE/logic_studio.html"; then pass "Studio server answers on $BASE"
else fail "Studio server not answering on $BASE — start ./qemu/studio.sh first"; echo; echo "== $NPASS passed, $NFAIL failed, $NSKIP skipped =="; exit 1; fi
. "$ROOT/qemu/twin-status.sh"
if twin_detect "$SERIAL_SOCK" 5; then pass "twin running (pid $TWIN_PID, $TWIN_WHICH)"; else fail "no running twin on $SERIAL_SOCK"; fi

echo "-- 1. unit tests (no twin needed)"
python3 bus-analysis/tools/test_trace_to_sigrok.py >/tmp/ltt-unit1.log 2>&1 && pass "converter unit tests (trace_to_sigrok)" || fail "converter unit tests — see /tmp/ltt-unit1.log"
python3 qemu/test_bus_capture_unit.py >/tmp/ltt-unit2.log 2>&1 && pass "bus-capture unit tests (QMP sequence, files, Studio bundle rules)" || fail "bus-capture unit tests — see /tmp/ltt-unit2.log"

echo "-- 2-4. live bus capture, endpoint security, identity, audit log"
python3 - "$BASE" "$CON" "$QMP_SOCK" <<'PY' | tally
import http.client, json, os, re, socket, sys, threading, time, urllib.parse
BASE, CON, QMP = sys.argv[1], int(sys.argv[2]), sys.argv[3]
u = urllib.parse.urlparse(BASE)
def req(method, path, origin=None, timeout=60):
    c = http.client.HTTPConnection(u.hostname, u.port, timeout=timeout)
    h = {"Origin": origin} if origin else {}
    c.request(method, path, headers=h); r = c.getresponse(); body = r.read(); c.close()
    return r.status, body
def jreq(method, path, origin=None, timeout=60):
    st, b = req(method, path, origin, timeout)
    try: return st, json.loads(b)
    except ValueError: return st, {"raw": b[:200].decode("latin1")}
def console(cmd, wait=3.0, until=None):
    # The console port is one shared stream: first drain whatever an earlier client left in
    # flight, then read until OUR command's echo is followed by the next prompt (a reply can
    # arrive in several chunks, so stopping at the first marker would cut it short).
    s = socket.create_connection(("127.0.0.1", CON), timeout=3); s.settimeout(0.3)
    drain_end = time.time() + 0.6
    while time.time() < drain_end:
        try:
            if not s.recv(65536): break
        except socket.timeout: break
    s.sendall((cmd + "\n").encode()); out = b""; end = time.time() + wait
    echo = cmd.encode()
    while time.time() < end:
        try: out += s.recv(65536)
        except socket.timeout: pass
        i = out.find(echo)
        if i >= 0 and b"plc> " in out[i + len(echo):]: break
    s.close(); return out.decode("utf-8", "replace")
def qmp(cmds):
    s = socket.socket(socket.AF_UNIX); s.settimeout(3); s.connect(QMP); f = s.makefile("rw"); f.readline(); out = []
    for ex, a in [("qmp_capabilities", None)] + cmds:
        m = {"execute": ex}; m.update({"arguments": a} if a else {}); f.write(json.dumps(m) + "\n"); f.flush()
        while True:
            r = json.loads(f.readline())
            if "return" in r or "error" in r: break
        out.append(r)
    s.close(); return out[1:]

st, f0 = jreq("GET", "/field")
di0 = f0.get("di", 4) if f0.get("ok") else 4
print(("PASS" if f0.get("ok") else "FAIL") + " GET /field reads the expander: di=%s relays=%s" % (f0.get("di"), f0.get("relays")))

# --- live capture while IN1 (di0) is pressed at the chip
res = {}
def cap(): res["r"] = jreq("POST", "/bus/capture?seconds=6", timeout=240)
t = threading.Thread(target=cap); t.start(); time.sleep(2.0)
req("POST", "/field?bit=0&v=1"); time.sleep(1.5); req("POST", "/field?bit=0&v=0"); t.join()
st, j = res.get("r", (0, {}))
ok = st == 200 and j.get("ok")
print(("PASS" if ok else "FAIL") + " POST /bus/capture?seconds=6 on the running twin: %s" % (
    "%s transactions, files %s" % (j["summary"]["frames"], ", ".join(sorted(j["files"]))) if ok else j.get("error", st)))
if ok:
    print(("PASS" if j["format"] == "observe" else "FAIL") + " trace source: %s (%s)" % (j["format"], ", ".join(j["trace_events"])))
    print(("PASS" if len(j["field"]["writes"]) == 2 else "FAIL") + " both /field writes recorded during the capture (%d)" % len(j["field"]["writes"]))
    sg = j.get("sigrok", {})
    print(("PASS" if sg.get("ok") and "write: 59" in sg.get("addresses", {}) else "FAIL") +
          " sigrok-cli decodes 0x59 on the live capture: %s" % (sg.get("addresses") or sg.get("error")))
    reads = [int(c["value"], 16) for c in sg.get("changes_0x59", []) if c["op"] == "read" and c["reg"] == "0x00"]
    saw = any(v & 0x10 for v in reads) and any(not v & 0x10 for v in reads)
    print(("PASS" if saw else "FAIL") + " the IN1 press is on the bus: 0x59 reg 0x00 bit 4 changed (%s)" % [hex(v) for v in reads][:6])
    w = [c["value"] for c in sg.get("changes_0x59", []) if c["op"] == "write" and c["reg"] == "0x02"]
    print(("PASS" if w else "FAIL") + " relay byte written to 0x59 reg 0x02 (values seen: %s)" % w[:6])
    sr = j["files"].get("sr")
    if sr:
        stx, b = req("GET", "/bus/files/" + sr)
        print(("PASS" if stx == 200 and len(b) > 100 else "FAIL") + " .sr downloadable from /bus/files/ (%d bytes); on disk: %s/%s" % (len(b), j["repo_path"], sr))
    else:
        print("FAIL no .sr written (is sigrok-cli installed?)")
    print(("PASS" if j["files"].get("pvs") else "FAIL") + " PulseView session file beside the .sr: %s" % j["files"].get("pvs"))
    print(("PASS" if j["files"].get("studio") else "FAIL") + " Studio bundle written (%s)" % (j["files"].get("studio") or j.get("studio_bundle_note")))
    print(("PASS" if not j["summary"]["warnings"] else "FAIL") + " converter warnings: %s" % (j["summary"]["warnings"] or "none"))
    print("info drawing: nudged=%s compressed=%s span=%ss (virtual time)" % (j["summary"]["drawing"]["nudged"],
          j["summary"]["drawing"]["compressed"], j["summary"]["drawing"]["span_s"]))
try:
    r = qmp([("trace-event-get-state", {"name": "esp32_i2c_observe"})])[0]
    state = r.get("return", [{}])[0].get("state")
    print(("PASS" if state == "disabled" else "FAIL") + " tracing switched off again after the capture (state=%s)" % state)
except Exception as e:
    print("SKIP could not ask QMP for the trace state (%s)" % e)
st, j2 = jreq("POST", "/bustap?secs=1", timeout=120)
print(("PASS" if st == 200 and j2.get("ok") else "FAIL") + " POST /bustap?secs=1 alias (%s)" % (j2.get("id") or j2.get("error")))

# --- endpoint security (same pattern as /field)
st, _ = req("POST", "/bus/capture?seconds=1", origin="http://evil.example")
print(("PASS" if st == 403 else "FAIL") + " capture from another site's page refused (HTTP %d)" % st)
st, _ = req("GET", "/bus/capture?seconds=1")
print(("PASS" if st == 405 else "FAIL") + " capture by GET refused (HTTP %d)" % st)
st, _ = req("GET", "/bus/files/..%2f..%2fqemu%2frun.sh")
print(("PASS" if st == 404 else "FAIL") + " /bus/files/ refuses names outside the capture pattern (HTTP %d)" % st)
st, _ = req("POST", "/fw/api/log")
print(("PASS" if st == 405 else "FAIL") + " /fw/api/log is read-only (POST -> HTTP %d)" % st)

# --- identity of the running image vs the firmware's own report
st, idj = jreq("GET", "/twin/identity")
elf = ((idj.get("app") or {}).get("elf_sha256") or "")
print(("PASS" if re.fullmatch(r"[0-9a-f]{64}", elf) else "FAIL") + " /twin/identity: %s profile, %s, boot slot %s, ELF %s" % (
    idj.get("profile"), idj.get("image"), idj.get("selected_slot"), elf[:16]))
out = console("diag json", 4.0, b'"elf_sha256"')
m = re.search(r'"elf_sha256":"([0-9a-f]{64})"', out)
if m:
    print(("PASS" if m.group(1) == elf else "FAIL") + " plc> diag json elf_sha256 %s the image's boot slot" % ("matches" if m.group(1) == elf else "DIFFERS from"))
else:
    print("SKIP no diag json reply on the console port %d" % CON)

# --- audit log path used by Provenance Studio
req("POST", "/modbus?fc=5&addr=8&value=0")               # aux_coil0: unassigned, no VM binding -> one audit row
time.sleep(1.0)
st, lg = jreq("GET", "/fw/api/log")
if isinstance(lg, list):
    print("PASS /fw/api/log proxies the firmware's audit log (%d lines, newest: %s)" % (len(lg), lg[0] if lg else "-"))
elif lg.get("shadowed"):
    print("PASS /fw/api/log reports the firmware HTTP port is held by this Studio server (page falls back to the console)")
    print("info " + lg.get("fix", ""))
else:
    print("FAIL /fw/api/log: %s" % lg.get("error"))
out = console("log", 3.0)
rows = [l for l in out.splitlines() if re.match(r"^\s{2}\d+ [A-Za-z0-9]+(/\S*)? .+", l)]   # e.g. "  11859 MODBUS/tcp/127.0.0.1:59724 WRITE coil 8"
print(("PASS" if rows else "FAIL") + " console fallback: plc> log returns audit rows (%d, last: %s)" % (len(rows), rows[-1].strip() if rows else "-"))
print(("PASS" if any("coil 8" in r for r in rows) or (isinstance(lg, list) and any("coil 8" in r for r in lg)) else "FAIL")
      + " the coil-8 write left an audit row")
req("POST", "/field?di=%d" % di0)
print("info input byte restored to 0x%02x" % di0)
PY

echo "-- 5. pages in a browser (Playwright)"
if node -e "require('playwright')" >/dev/null 2>&1; then
  node - "$BASE" <<'JS' | tally
const { chromium } = require('playwright'); const BASE = process.argv[2];
const out = (ok, msg) => console.log((ok === null ? 'SKIP ' : ok ? 'PASS ' : 'FAIL ') + msg);
(async () => {
  const b = await chromium.launch();
  try {
    let p = await b.newPage(); let errs = []; p.on('pageerror', e => errs.push(e.message));
    await p.goto(BASE + '/provenance_studio.html'); await p.waitForTimeout(3000);
    await p.request.post(BASE + '/modbus?fc=5&addr=8&value=0', { headers: { Origin: BASE } });
    await p.waitForTimeout(6500);
    const pill = await p.$eval('#modepill', e => e.textContent), bar = await p.$eval('#livebar', e => e.textContent);
    const walk = await p.$eval('#walkbanner', e => e.classList.contains('show'));
    const lane = await p.$$eval('#lane .ev', a => a.map(e => e.textContent));
    out(/LIVE/.test(pill) && !walk, `Provenance Studio opens LIVE by default (pill "${pill}", walkthrough hidden)`);
    out(lane.some(t => /coil 8/.test(t)), `Provenance Studio shows the live coil-8 audit row (${lane.length} events)`);
    console.log('info ' + bar.slice(0, 220));
    out(errs.length === 0, 'Provenance Studio: no page errors ' + (errs[0] || ''));
    await p.click('#walkBtn'); await p.waitForTimeout(500);
    out(await p.$eval('#walkbanner', e => e.classList.contains('show')) && /OFFLINE/.test(await p.$eval('#modepill', e => e.textContent)),
        'offline walkthrough only on click, clearly labelled');
    await p.close();

    p = await b.newPage(); errs = []; p.on('pageerror', e => errs.push(e.message));
    await p.goto(BASE + '/signal_journey.html'); await p.waitForTimeout(800);
    out(await p.$$eval('.illus', a => a.length) >= 1 && /illustrative/i.test(await p.textContent('h2.sect')), 'Signal Journey: opening map labelled "illustrative, not live"');
    await p.click('tr[data-id=p1] [data-read=field]'); await p.waitForTimeout(1200);
    const v = await p.$eval('tr[data-id=p1] textarea[data-k=value]', e => e.value), prov = await p.$eval('tr[data-id=p1] [data-prov]', e => e.textContent);
    out(/RELAY 1 \(relay0\)=/.test(v) && /live from your twin/.test(prov), 'Signal Journey capstone row 1 filled live from /field: ' + v.slice(0, 80));
    await p.click('tr[data-id=p5] [data-read=identity]'); await p.waitForTimeout(1200);
    out(/ELF SHA-256 [0-9a-f]{64}/.test(await p.$eval('tr[data-id=p5] textarea[data-k=value]', e => e.value)), 'Signal Journey capstone row 5 filled from /twin/identity');
    out(errs.length === 0, 'Signal Journey: no page errors ' + (errs[0] || ''));
    await p.close();

    p = await b.newPage(); errs = []; p.on('pageerror', e => errs.push(e.message));
    await p.goto(BASE + '/logic_studio.html'); await p.waitForTimeout(1500);
    await p.evaluate(() => recordTwinBus(3)); await p.waitForTimeout(25000);
    const note = await p.$eval('#busTapNote', e => e.textContent), summ = await p.$eval('#busSummary', e => e.textContent);
    out(/LIVE TAP of your twin/.test(note), 'Studio "Record twin bus" button: ' + note.slice(0, 120));
    out(/QEMU/.test(summ), 'Studio Bus monitor imported the live capture through its own validator: ' + summ.slice(0, 90));
    out(errs.length === 0, 'Logic Studio: no page errors ' + (errs[0] || ''));
  } catch (e) { out(false, 'browser checks crashed: ' + e.message); }
  await b.close();
})();
JS
else
  skip "Playwright not installed (npm i playwright && npx playwright install --with-deps chromium)"
fi

echo "-- 6. tshark in the Codespace"
if command -v tshark >/dev/null 2>&1; then
  pass "tshark installed: $(tshark --version 2>/dev/null | head -1)"
  if sudo -n true 2>/dev/null; then
    sudo -n timeout 6 tshark -i lo -f "tcp port 502" -a duration:4 -w /tmp/ltt-502.pcap >/dev/null 2>&1 &
    sleep 1.5; for _ in 1 2 3; do curl -s -o /dev/null "$BASE/modbus?fc=3&addr=25&count=1"; sleep 0.5; done; wait
    # the capture ran as root, so its pcap is root-owned (0600): read it back with sudo too
    N="$(sudo -n tshark -r /tmp/ltt-502.pcap -Y modbus 2>/dev/null | wc -l)"
    [ "${N:-0}" -ge 2 ] && pass "sudo tshark captured live Modbus on lo:502 ($N frames)" || fail "tshark saw $N Modbus frames on lo:502"
  else skip "no passwordless sudo for a live tshark capture"; fi
else fail "tshark not installed (re-run .devcontainer/postCreate.sh)"; fi
command -v sigrok-cli >/dev/null 2>&1 && pass "sigrok-cli installed: $(sigrok-cli --version 2>/dev/null | head -1)" || fail "sigrok-cli not installed"

echo "-- 7. SBOM of the running image"
if [ "${SKIP_SBOM:-0}" = "1" ]; then skip "SKIP_SBOM=1"
else
  ./sbom/scan-running.sh >/tmp/ltt-sbom.log 2>&1; RC=$?
  [ "$RC" = 0 ] && pass "./sbom/scan-running.sh ran (log /tmp/ltt-sbom.log)" || fail "./sbom/scan-running.sh exited $RC (log /tmp/ltt-sbom.log)"
  python3 - <<'PY' | tally
import json, os, urllib.request
o = "sbom/out"
for f in ("firmware.spdx.json", "firmware.cdx.json", "running-identity.json"):
    print(("PASS" if os.path.isfile(os.path.join(o, f)) else "FAIL") + " sbom/out/" + f)
try:
    ident = json.load(open(os.path.join(o, "running-identity.json")))
    live = json.load(urllib.request.urlopen("http://127.0.0.1:%s/twin/identity" % os.environ.get("BRIDGE_PORT", "8088")))
    same = ident["app"]["elf_sha256"] == (live.get("app") or {}).get("elf_sha256")
    print(("PASS" if same else "FAIL") + " SBOM describes the image the twin booted (ELF %s)" % ident["app"]["elf_sha256"][:16])
    print("info build match: " + ident["build_match"]["how"])
    rc = ident.get("running_check")
    print(("PASS" if rc and rc["agree"] else "SKIP" if not rc else "FAIL") + " plc> diag json agrees with the scanned slot" + ("" if rc else " (no console reply)"))
except Exception as e:
    print("FAIL identity check: %s" % e)
for f, what in (("risk.txt", "RISK table (grype)"), ("hmi.spdx.json", "HMI container SBOM (needs the plant built)")):
    p = os.path.join(o, f)
    print(("PASS" if os.path.isfile(p) else "SKIP") + " " + what + (": " + open(p).read().splitlines()[0][:100] if os.path.isfile(p) and f == "risk.txt" else ""))
PY
fi

echo "-- 8. ATT&CK v19 IDs + formal verification"
LEFT="$(grep -rnE 'T08(55|56|57|39|12|91|03|04|05|33)\b' -I . --exclude-dir=.git --exclude-dir=node_modules | grep -v 'formerly' || true)"
[ -z "$LEFT" ] && pass "no legacy ATT&CK IDs outside a 'formerly' note" || { fail "legacy ATT&CK IDs remain:"; echo "$LEFT" | head -5; }
if python3 -c 'import z3' 2>/dev/null; then
  python3 formal-verify/verify_controls.py --profile verified --assert-proven >/tmp/ltt-fv.log 2>&1 \
    && pass "formal-verify: every control PROVEN on the verified build" || fail "formal-verify (see /tmp/ltt-fv.log)"
else skip "z3 not installed (pip install z3-solver)"; fi

echo
echo "== $NPASS passed, $NFAIL failed, $NSKIP skipped =="
[ "$NFAIL" -eq 0 ]
