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

def find_tool(name_glob, explicit):
    """Locate an xtensa cross tool: explicit path, PATH, or PlatformIO packages."""
    if explicit and os.path.exists(explicit):
        return explicit
    from shutil import which
    import glob
    for cand in ("xtensa-esp32s3-elf-"+name_glob, "xtensa-esp-elf-"+name_glob,
                 "xtensa-esp32s2-elf-"+name_glob):
        p = which(cand)
        if p: return p
    home = os.path.expanduser("~/.platformio/packages")
    hits = glob.glob(os.path.join(home, "**", "xtensa-esp*-"+name_glob), recursive=True)
    return hits[0] if hits else None

def enrich_from_elf(rows, elf, objdump=None, addr2line=None):
    """Derive {func, addr} for each row's (file,line) from the ELF's DWARF.

    No hardcoded values: the DWARF decoded-line table gives (file,line)->address
    and addr2line gives the enclosing function, so this re-derives correctly for
    EVERY build. Rows whose exact line was optimized away fall back to the nearest
    later line in the same file (addr_exact=false). If the toolchain or ELF is
    absent the rows are returned unchanged (address fields simply omitted)."""
    od = find_tool("objdump", objdump)
    a2l = find_tool("addr2line", addr2line)
    if not (elf and os.path.exists(elf) and od and a2l):
        return {"enriched": False, "reason": "elf or xtensa toolchain not found",
                "objdump": od, "addr2line": a2l, "elf": elf}
    # 1) build (basename,line) -> min address from the DWARF line table
    out = subprocess.run([od, "--dwarf=decodedline", elf],
                         capture_output=True, text=True, errors="replace").stdout
    line_addr = {}          # (basename, line) -> address(int)
    file_lines = {}         # basename -> sorted list of (line, addr)
    for ln in out.splitlines():
        # columns: <CU/File>  <Line>  <Address>  ... ; formats vary, so scan tokens
        toks = ln.split()
        if len(toks) < 3: continue
        # find a hex address token and an int line token
        addr = None
        for t in reversed(toks):
            if t.startswith("0x"):
                try: addr = int(t, 16); break
                except ValueError: pass
        if addr is None: continue
        # the filename is toks[0] (may be a basename or a path); line is toks[1]
        try: line = int(toks[1])
        except ValueError: continue
        base = os.path.basename(toks[0])
        key = (base, line)
        if key not in line_addr or addr < line_addr[key]:
            line_addr[key] = addr
        file_lines.setdefault(base, []).append((line, addr))
    for b in file_lines: file_lines[b].sort()
    # 2) resolve each row
    func_cache = {}
    def func_of(addr):
        if addr in func_cache: return func_cache[addr]
        r = subprocess.run([a2l, "-f", "-C", "-e", elf, hex(addr)],
                           capture_output=True, text=True, errors="replace").stdout.splitlines()
        fn = r[0].strip() if r else "??"
        func_cache[addr] = fn
        return fn
    n_exact = n_near = n_miss = 0
    for row in rows:
        base, line = row["file"], row["line"]
        addr = line_addr.get((base, line)); exact = True
        if addr is None:                      # nearest later line in same file
            exact = False
            cand = [a for (l, a) in file_lines.get(base, []) if l >= line]
            addr = min(cand) if cand else None
        if addr is None:
            n_miss += 1; continue
        row["addr"] = hex(addr)
        row["func"] = func_of(addr)
        row["addr_exact"] = exact
        n_exact += exact; n_near += (not exact)
    return {"enriched": True, "elf": os.path.abspath(elf),
            "objdump": od, "addr2line": a2l,
            "resolved_exact": n_exact, "resolved_nearest": n_near, "unresolved": n_miss,
            "build_note": "func+addr re-derived from this ELF's DWARF; rebuild the firmware "
                          "and re-run with --elf to keep them true."}

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

    # Optional DWARF enrichment: --elf <firmware.elf> adds {func, addr} per row.
    import argparse
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--elf"); ap.add_argument("--objdump"); ap.add_argument("--addr2line")
    args, _ = ap.parse_known_args()
    elf_meta = None
    if args.elf:
        elf_meta = enrich_from_elf(rows, args.elf, args.objdump, args.addr2line)

    meta = {
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds").replace("+00:00","Z"),
        "source": "sbom/firmware/src/*.cpp plc_log() call sites",
        "elf": elf_meta,
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
