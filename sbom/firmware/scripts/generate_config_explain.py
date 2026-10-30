#!/usr/bin/env python3
# Emit src/config_explain_table.inc (the EXPLAIN[] rows) from config-catalog.json.
# The catalog is the single source of truth; the firmware table is never hand-edited.
# Mirrors generate_point_map.py: run() writes, run(check=True) refuses a stale build.
import json, hashlib, os
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CAT  = os.path.join(HERE, "config-catalog.json")
OUT  = os.path.join(HERE, "src", "config_explain_table.inc")

def esc(s): return (s or "").replace("\\", "\\\\").replace('"', '\\"')

def render():
    raw = open(CAT, "rb").read()
    cat = json.loads(raw)
    sha = hashlib.sha256(raw).hexdigest()[:16]
    rows = []
    for k, v in cat.get("concepts", {}).items():
        # concepts carry an explicit short "key" so the plc> console verb stays
        # friendly (explain ip) even though the catalog object-key is descriptive
        # (ip_address) for the Studio's concept links.
        rows.append((v.get("key", k), "concept", v.get("plain",""), v.get("firmware",""), v.get("security",""), v.get("console","")))
    order = {"wifi":0, "ethernet":1, "shared":2}
    props = sorted(cat.get("properties", {}).items(), key=lambda kv: order.get(kv[1].get("group",""), 9))
    for k, v in props:
        rows.append((k, v.get("group","other"), v.get("plain",""), v.get("firmware",""), v.get("security",""), v.get("console","")))
    body  = "// GENERATED from config-catalog.json by scripts/generate_config_explain.py - DO NOT EDIT.\n"
    body += "// catalog sha256[:16] = %s\n" % sha
    for k, g, what, fw, sec, cmd in rows:
        body += '  {"%s","%s","%s","%s","%s","%s"},\n' % (esc(k), esc(g), esc(what), esc(fw), esc(sec), esc(cmd))
    return body, len(rows), sha

def run(check=False):
    body, n, sha = render()
    cur = open(OUT).read() if os.path.exists(OUT) else None
    if cur == body:
        print("config_explain_table.inc up to date (%d rows, sha %s)" % (n, sha)); return
    if check:
        raise SystemExit(
            "config_explain_table.inc is STALE (catalog sha %s). "
            "Run: python3 scripts/generate_config_explain.py, then commit." % sha)
    open(OUT, "w").write(body)
    print("wrote config_explain_table.inc (%d rows, sha %s)" % (n, sha))

if __name__ == "__main__":
    import sys
    run(check="--check" in sys.argv)
