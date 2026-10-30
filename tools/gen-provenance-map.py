#!/usr/bin/env python3
"""
gen-provenance-map.py  --  build the STATIC half of the Logic-Studio
static<->dynamic association.

It parses every plc_log(...) call site in the firmware source and emits
web/public/firmware-provenance-map.json: one row per audited write/refusal/
read/enumeration event the firmware can emit, carrying the source file:line,
the WriteSource, the message template, a matcher regex that recognises the
LIVE rendered string from /api/log, an inferred category, and any CWE tagged
in the message.  Logic Studio loads this map, taps /api/log, and annotates
each live event with where in the firmware it came from.

Reproducible: run it again after editing the firmware and the map regenerates.
Verify a row's address against DWARF with:
    xtensa-...-addr2line -f -e firmware.elf <addr>
Nothing here runs or flashes the device; it only reads source.
"""
import json, os, re, sys, subprocess, datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC  = os.path.join(ROOT, "sbom", "firmware", "src")
OUT  = os.path.join(ROOT, "web", "public", "firmware-provenance-map.json")

# WriteSource enum (plc_core.h) -> label shown in the Studio provenance lane.
SRC_LABEL = {
    "SRC_SYSTEM":"system","SRC_MODBUS":"modbus","SRC_USB":"usb","SRC_WEB":"web",
    "SRC_LOGIC":"logic","SRC_DNP3":"dnp3","SRC_S7":"s7","SRC_OPCUA":"opcua",
    "SRC_IEC104":"iec104","SRC_ENIP":"enip","SRC_BACNET":"bacnet",
}

# Match a plc_log( call and capture the first arg (WriteSource) and the first
# string-literal format argument on the same logical line.
CALL = re.compile(r'plc_log\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*[^,]*,\s*"((?:[^"\\]|\\.)*)"')

def category(fmt):
    f = fmt.upper()
    if f.startswith("WRITE COIL") or f.startswith("WRITE REG") or f.startswith("WRITE COILS") or f.startswith("WRITE REGS"):
        return "write"
    if f.startswith("FORCE") or f.startswith("UNFORCE") or "CLEAR ALL FORCES" in f:
        return "force"
    if f.startswith("REFUSED") or "DENIED" in f or "REJECTED" in f:
        return "refused"
    if f.startswith("DNP3 DIRECT_OPERATE") or "C_SC_NA_1" in f or "CROB" in f and "DENIED" not in f:
        return "write"
    if "READ" in f or "INTERROGATION" in f or "SNAPSHOT" in f or "WHO-IS" in f \
       or "ENUMERATION" in f or "FINGERPRINT" in f or "IDENTIFICATION" in f \
       or "HELLO" in f or "HANDSHAKE" in f or "GETENDPOINTS" in f or "LISTIDENTITY" in f \
       or "SETUP COMMUNICATION" in f or "READSZL" in f or "REGISTERSESSION" in f \
       or "SENDRRDATA" in f or "STARTDT" in f:
        return "recon"
    if f.startswith("MODE") or "MODE" in f:
        return "mode"
    return "system"

def cwe_of(fmt):
    m = re.search(r'CWE-\d+', fmt)
    return m.group(0) if m else None

def to_regex(fmt):
    """Turn a printf format string into a regex that matches the rendered line."""
    out, i = [], 0
    while i < len(fmt):
        c = fmt[i]
        if c == '%':
            # consume a printf conversion spec
            j = i + 1
            while j < len(fmt) and fmt[j] in "-+ #0123456789.*lhLzjt":
                j += 1
            conv = fmt[j] if j < len(fmt) else ''
            if conv in "dioxXu":
                out.append(r'(-?[0-9A-Fa-fx]+)')
            elif conv in "fFgGeE":
                out.append(r'(-?[0-9.]+)')
            elif conv in "sS":
                out.append(r'(.*?)')
            elif conv == 'c':
                out.append(r'(.)')
            elif conv == '%':
                out.append('%')
            else:
                out.append(r'.*?')
            i = j + 1
        else:
            out.append(re.escape(c))
            i += 1
    # collapse a trailing greedy wildcard group to end-anchored
    return '^' + ''.join(out) + '$'

def one_liners(cat, src_label, fmt, cwe):
    """plain / engineering / cyber gloss for the annotation popover."""
    if cat == "write":
        return {
            "plain": f"An output actually changed, requested over {src_label}.",
            "eng":   "A commanded point write that passed mode + arbiter checks and reached the process image.",
            "cyber": "Authored, audited write: it is attributable to a source and appears in the provenance log. Contrast an output that moves with NO matching entry here."
        }
    if cat == "force":
        return {
            "plain": f"An output was forced on/off by hand, via {src_label}.",
            "eng":   "A maintenance force through the audited plc_force_coil() path -- bypasses the running logic but is still logged.",
            "cyber": "Forces are legitimate but high-trust. Every force here is attributable; a relay that changes with no force and no write is the tell for a memory-corruption path."
        }
    if cat == "refused":
        extra = f" Tagged {cwe}." if cwe else ""
        return {
            "plain": f"A write was refused before it could change anything.{extra}",
            "eng":   "The guard (mode gate / protocol arbiter / ACL) rejected the command; the process image did not change.",
            "cyber": f"This is the control working. {cwe or 'The refusal'} is the weakness that WOULD exist without the gate -- the log proves the gate fired."
        }
    if cat == "recon":
        return {
            "plain": f"A device answered a discovery/identify request on {src_label}.",
            "eng":   "A read/enumeration/handshake -- no output changed, but the device disclosed identity or state.",
            "cyber": "Reconnaissance surface. No write, but it maps the device for an attacker (ATT&CK for ICS discovery)."
        }
    if cat == "mode":
        return {
            "plain": "The controller changed or refused to change run mode.",
            "eng":   "PROGRAM/RUN/REMOTE transition through plc_set_mode(), with its guard.",
            "cyber": "Mode is the master gate for remote writes. Who moved it, and from where, is the first question in an incident."
        }
    return {
        "plain": "A system-level event.",
        "eng":   "Core lifecycle / fault / project event.",
        "cyber": "Context for the timeline; rarely attributable to a remote peer."
    }

def addr_index(elf):
    """Optional: map source file:line -> symbol via the ELF, if tools present."""
    return {}   # addr2line enrichment is optional; left as a hook, see README.

def main():
    rows = []
    if not os.path.isdir(SRC):
        print(f"firmware src not found: {SRC}", file=sys.stderr); sys.exit(1)
    for fn in sorted(os.listdir(SRC)):
        if not fn.endswith(".cpp"): continue
        path = os.path.join(SRC, fn)
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        # join continuation lines so a plc_log(...) split across lines still matches
        text = "".join(lines)
        for m in CALL.finditer(text):
            srcarg, fmt = m.group(1), m.group(2)
            if srcarg not in SRC_LABEL:   # skip non-WriteSource first args (var names)
                # some calls pass a variable 'src'/'cur_src'; tag them unknown
                label = "runtime"
            else:
                label = SRC_LABEL[srcarg]
            line_no = text.count("\n", 0, m.start()) + 1
            cat = category(fmt)
            cwe = cwe_of(fmt)
            rows.append({
                "id": f"{fn}:{line_no}",
                "file": fn,
                "line": line_no,
                "src": srcarg if srcarg in SRC_LABEL else srcarg,
                "src_label": label,
                "category": cat,
                "cwe": cwe,
                "template": fmt,
                "match": to_regex(fmt),
                "gloss": one_liners(cat, label, fmt, cwe),
            })
    rows.sort(key=lambda r: (r["file"], r["line"]))
    meta = {
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds").replace("+00:00","Z"),
        "source": "sbom/firmware/src/*.cpp plc_log() call sites",
        "live_feed": "/api/log  (web_api_log_json) and serial 'log' / busstream",
        "note": "STATIC half of the Logic-Studio association. A live /api/log line is "
                "matched to a row by its 'match' regex -> source file:line + CWE. "
                "An output change with no matching WRITE/FORCE row is the provenance "
                "gap that reveals an unaudited (memory-corruption) write.",
        "count": len(rows),
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump({"meta": meta, "events": rows}, f, indent=1)
    print(f"wrote {OUT}  ({len(rows)} event sites)")
    # quick category census for sanity
    from collections import Counter
    c = Counter(r["category"] for r in rows)
    print("  categories:", dict(c))
    print("  with CWE:  ", sum(1 for r in rows if r["cwe"]))

if __name__ == "__main__":
    main()
