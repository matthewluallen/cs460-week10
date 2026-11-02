#!/usr/bin/env python3
"""ai-detector/detect.py — run the detector live on the broker (or replay a file).

    # vulnerable: model only, learns online, raw score on an open /score endpoint
    python ai-detector/detect.py --profile vuln --model ai-detector/out/model.pkl --adapt --http 8095
    # secure: gate + model + accelerometer fusion, frozen, signed artifact from the registry
    python ai-detector/detect.py --profile secure --registry ai-detector/registry
    # offline: replay a recording instead of listening
    python ai-detector/detect.py --profile secure --model ai-detector/out/model-signed --replay FILE.jsonl

For every tilt reading it prints and publishes  ai/<id>/verdict  {"verdict": "OK"|"ALARM", ...}.
It never writes to the controller: the detector is an alarm, not a control (keep AI out of the
actuation path).
"""
import argparse
import json
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aidet  # noqa: E402


def load(a):
    if a.registry:
        return aidet.load_from_registry(a.registry, a.profile)
    if a.profile == "vuln":
        return aidet.load_pickle(a.model)
    return aidet.load_signed(a.model)


def replay(det, a):
    frames = aidet.frames_from_messages(aidet.read_jsonl(a.replay), a.id)
    vs = det.evaluate_many(frames, a.profile)
    n = sum(v.verdict == "ALARM" for v in vs)
    why = {}
    for v in vs:
        for r in v.reasons:
            k = r.split(":")[0]
            why[k] = why.get(k, 0) + 1
    print(f"{a.replay}: {n}/{len(vs)} readings ALARM ({a.profile})  by stage: {why or '-'}")
    last = None
    for f, v in zip(frames, vs):
        if v.verdict != last:
            print(f"  t={f.t - frames[0].t:7.1f}s  β={f.beta:7.1f}°  {v.verdict}  {'; '.join(v.reasons)}")
            last = v.verdict
    return 0


def serve_oracle(oracle, port, lock):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            u = urlparse(self.path)
            if u.path != "/score":
                self.send_error(404)
                return
            q = {k: v[0] for k, v in parse_qs(u.query).items()}
            try:
                with lock:
                    body = oracle.query(q.get("beta", 0), q.get("gamma", 0), q.get("dbeta", 0),
                                        q.get("dgamma", 0), client=self.client_address[0])
                code = 200
            except aidet.AidetError as e:
                body, code = {"error": str(e)}, 429 if "budget" in str(e) else 403
            except ValueError:
                body, code = {"error": "beta, gamma, dbeta, dgamma must be numbers"}, 400
            data = json.dumps(body).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    srv = ThreadingHTTPServer(("127.0.0.1", port), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profile", choices=["vuln", "secure"], default="vuln")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--model", help="model.pkl (vuln) or a signed artifact directory (secure)")
    src.add_argument("--registry", help="load registry/current/ the way a fleet would")
    ap.add_argument("--broker", default=os.environ.get("BROKER", "127.0.0.1:1883"))
    ap.add_argument("--id", help="only this phone id")
    ap.add_argument("--replay", help="evaluate a JSONL recording instead of listening live")
    ap.add_argument("--adapt", action="store_true", help="vuln only: keep learning from accepted readings")
    ap.add_argument("--http", type=int, metavar="PORT", help="serve GET /score on 127.0.0.1:PORT")
    ap.add_argument("--oracle", choices=["on", "off"], help="secure: enable the /score endpoint (default off)")
    ap.add_argument("--quiet", action="store_true", help="print only verdict changes")
    a = ap.parse_args()
    try:
        det = load(a)
    except aidet.AidetError as e:
        print(f"REFUSED to load the model: {e}")
        return 2
    m = det.meta.get("manifest")
    print(f"[detector] profile={a.profile}  model={det.meta.get('algorithm', '?')}  "
          f"dataset={(det.meta.get('dataset') or {}).get('sha256', '?')[:16]}…"
          + (f"  signed manifest v{m['version']} verified" if m else "  (no signature checked)"))
    if a.adapt:
        if a.profile != "vuln":
            print("  --adapt ignored: the secure profile's model only changes through a signed update")
        else:
            det.enable_adapt()
            print("  online learning ON: every accepted reading is folded into the model")
    if a.replay:
        return replay(det, a)

    lock = threading.Lock()
    if a.http:
        oracle = aidet.Oracle(det, a.profile, enabled=(None if a.oracle is None else a.oracle == "on"))
        serve_oracle(oracle, a.http, lock)
        print(f"  /score on http://127.0.0.1:{a.http}/score?beta=…&gamma=…  "
              f"({'raw score + threshold' if a.profile == 'vuln' else ('verdict only, budgeted' if oracle.enabled else 'DISABLED')})")

    import paho.mqtt.client as mqtt
    trackers, last, counts = {}, {}, {}

    def on_message(c, _u, msg):
        parts = msg.topic.split("/")
        if len(parts) != 3 or (a.id and parts[1] != a.id):
            return
        try:
            p = json.loads(msg.payload)
        except ValueError:
            return
        pid = parts[1]
        f = trackers.setdefault(pid, aidet.Tracker()).feed(time.time(), parts[2], p)
        if f is None:
            return
        with lock:
            v = det.evaluate(f, a.profile)
        n = counts[pid] = counts.get(pid, 0) + 1
        c.publish(f"ai/{pid}/verdict", json.dumps({"t": round(f.t, 3), "beta": f.beta, **v.as_dict(a.profile)}))
        if not a.quiet or v.verdict != last.get(pid):
            extra = f"  score={v.score:.2f}" if (a.profile == "vuln" and v.score is not None) else ""
            print(f"{time.strftime('%H:%M:%S')} {pid:>10}  β={f.beta:7.1f}°  {v.verdict:5}{extra}  "
                  f"{'; '.join(v.reasons)}", flush=True)
        last[pid] = v.verdict
        if a.adapt and det.adapt and n % 200 == 0:
            print(f"  [online learning] refits so far: {det.adapt['refits']}", flush=True)

    host, _, port = a.broker.partition(":")
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"aidet-detect-{os.getpid()}")
    c.on_message = on_message
    c.connect(host, int(port or 1883), keepalive=30)
    c.subscribe([("phone/+/orient", 0), ("phone/+/accel", 0)])
    print(f"  listening on {a.broker} phone/+/orient + phone/+/accel — verdicts on ai/<id>/verdict  (Ctrl-C to stop)")
    try:
        c.loop_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
