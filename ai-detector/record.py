#!/usr/bin/env python3
"""ai-detector/record.py — record a baseline from the live broker (your phone, or phonesim).

    python ai-detector/record.py --seconds 300 --out ai-detector/out/baseline.jsonl
    python ai-detector/record.py --id <your-phone-id> --seconds 300 --out ai-detector/out/baseline.jsonl

Records phone/<id>/orient and phone/<id>/accel exactly as they arrive. Share BOTH Motion and
Orientation on the phone page, and during the recording do what "normal" means for the plant:
let it rest, and tip it past the limit a few times the way a real event would. Whatever is on
the wire while you record becomes "normal" to the model — including anything an attacker sends.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aidet  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--broker", default=os.environ.get("BROKER", "127.0.0.1:1883"))
    ap.add_argument("--id", help="only this phone id (default: every id)")
    ap.add_argument("--seconds", type=float, default=300)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    import paho.mqtt.client as mqtt
    msgs, ids = [], {}

    def on_message(_c, _u, m):
        parts = m.topic.split("/")
        if len(parts) != 3 or parts[2] not in ("orient", "accel"):
            return
        if a.id and parts[1] != a.id:
            return
        try:
            p = json.loads(m.payload)
        except ValueError:
            return
        msgs.append((time.time(), parts[1], parts[2], p))
        ids[parts[1]] = ids.get(parts[1], 0) + 1

    host, _, port = a.broker.partition(":")
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="aidet-record")
    c.on_message = on_message
    c.connect(host, int(port or 1883), keepalive=30)
    c.subscribe([("phone/+/orient", 0), ("phone/+/accel", 0)])
    c.loop_start()
    t0 = time.time()
    try:
        while time.time() - t0 < a.seconds:
            time.sleep(5)
            print(f"  {time.time() - t0:5.0f}/{a.seconds:.0f} s  messages={len(msgs)}  ids={ids}", flush=True)
    except KeyboardInterrupt:
        print("  stopped early")
    c.loop_stop()
    c.disconnect()
    aidet.write_jsonl(a.out, msgs)
    print(f"wrote {len(msgs)} messages -> {a.out}   sha256 {aidet.sha256_file(a.out)}")
    if not any(m[2] == "accel" for m in msgs):
        print("  WARNING: no accel messages — share Motion too, or the secure profile cannot corroborate")
    return 0


if __name__ == "__main__":
    sys.exit(main())
