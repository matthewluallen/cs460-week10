#!/usr/bin/env python3
"""run-scenario.py — drive the phone-sensor CALDERA operation over the REST API.

Waits for the Sandcat agent (landed on the engineer station by deploy-sandcat.sh) to check in,
starts the "CS460 Phone-Sensor False-Data Injection" operation with the phone-sensor fact
source, waits for it to finish, and prints the event log — the ability / technique / command /
status rows that are the attacker's record of the attack (and the same schema the Module 4 MP1
autograder grades).

    python3 run-scenario.py                      # from inside the caldera container
    CALDERA_URL=http://localhost:8888 python3 run-scenario.py

No dependencies beyond the standard library.
"""
import json
import os
import sys
import time
import urllib.error
import urllib.request

CALDERA = os.environ.get("CALDERA_URL", "http://localhost:8888")
KEY = os.environ.get("CALDERA_API_KEY", "ADMIN123")
GROUP = os.environ.get("AGENT_GROUP", "red")
ADVERSARY = "c5625793-3359-4d21-bfd8-e1f5bd6d9e6c"   # CS460 Phone-Sensor False-Data Injection
SOURCE = "e1972ca7-b248-4a58-98c6-e5c9ead542d8"      # CS460 Phone-Sensor Facts
PLANNER = "atomic"


def req(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(CALDERA + "/api/v2" + path, data=data, method=method,
                               headers={"KEY": KEY, "Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=30) as resp:
        t = resp.read().decode()
        return resp.status, (json.loads(t) if t else None)


def wait_for_server(timeout=120):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            req("GET", "/health")
            return True
        except (urllib.error.URLError, OSError):
            time.sleep(3)
    return False


def wait_for_agent(timeout=180):
    t0 = time.time()
    while time.time() - t0 < timeout:
        _, agents = req("GET", "/agents")
        red = [a for a in (agents or []) if a.get("group") == GROUP]
        if red:
            return red
        print(f"  waiting for an agent in group '{GROUP}' to check in "
              f"(run deploy-sandcat.sh on the engineer station) …", flush=True)
        time.sleep(4)
    return []


def main():
    print(f"== CS460 phone-sensor CALDERA scenario ==  server {CALDERA}")
    if not wait_for_server():
        sys.exit("CALDERA server did not become healthy")
    agents = wait_for_agent()
    if not agents:
        sys.exit(f"no agent in group '{GROUP}' — the dropper never landed. "
                 f"Run: docker compose -f stations/docker-compose.yml run --rm engineer /app/deploy-sandcat.sh")
    print("agent(s) checked in:", ", ".join(f"{a['paw']} on {a.get('host', '?')}" for a in agents))

    op = {"name": "phone-sensor-fdi", "adversary": {"adversary_id": ADVERSARY},
          "source": {"id": SOURCE}, "planner": {"id": PLANNER}, "group": GROUP,
          "state": "running", "autonomous": 1, "auto_close": True,
          "obfuscator": "plain-text", "jitter": "1/4", "visibility": 51}
    _, res = req("POST", "/operations", op)
    oid = res["id"]
    print(f"operation '{res['name']}' started (id {oid}); running the kill chain …")

    for _ in range(120):
        _, o = req("GET", f"/operations/{oid}")
        if o.get("state") in ("finished", "cleanup", "out_of_time", "paused"):
            break
        time.sleep(3)

    _, logs = req("POST", f"/operations/{oid}/event-logs", {"enable_agent_output": True})
    print(f"\n== event log ({len(logs or [])} steps) ==")
    for e in logs or []:
        am = e.get("attack_metadata", {})
        ab = e.get("ability_metadata", {})
        out = e.get("output")
        if isinstance(out, dict):                        # enable_agent_output=True returns a dict
            out = out.get("stdout") or out.get("stderr") or ""
        out = (out or "").strip().replace("\n", " ")
        print(f"\n[{e.get('status')}] {ab.get('ability_name')}  "
              f"({am.get('technique_id')} {am.get('technique_name')})")
        print(f"    $ {e.get('command')}")
        if out:
            print(f"    -> {out[:300]}")
    print(f"\nfull report: {CALDERA}/#/operations/{oid}  (or POST /api/v2/operations/{oid}/report)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
