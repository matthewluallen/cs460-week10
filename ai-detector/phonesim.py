#!/usr/bin/env python3
"""ai-detector/phonesim.py — a fake phone: write a scenario to a file, or publish it live.

    python ai-detector/phonesim.py --list
    python ai-detector/phonesim.py --scenario baseline --out ai-detector/out/sim-baseline.jsonl
    python ai-detector/phonesim.py --scenario crafted-spoof --publish 127.0.0.1:1883 --id sim1

--publish sends phone/<id>/orient and phone/<id>/accel to the lab broker (mqtt/share.sh, or the
stations stack) at real-time pace, exactly as the phone page would — so the controller, the
detector and Wireshark all see it. No phone needed. --speed 5 plays it 5x faster.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aidet  # noqa: E402
import scenarios  # noqa: E402


def mqtt_client(broker, client_id):
    import paho.mqtt.client as mqtt
    host, _, port = broker.partition(":")
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    c.connect(host, int(port or 1883), keepalive=30)
    c.loop_start()
    return c


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenario", default="baseline", choices=scenarios.NAMES)
    ap.add_argument("--list", action="store_true", help="list scenarios and exit")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--id", default="sim1", help="phone id to publish under (an id is a claim, not a login)")
    ap.add_argument("--out", help="write the messages to this JSONL file")
    ap.add_argument("--publish", metavar="HOST:PORT", help="publish live to this MQTT broker")
    ap.add_argument("--speed", type=float, default=1.0, help="playback speed for --publish")
    a = ap.parse_args()
    if a.list:
        for n in scenarios.NAMES:
            print(n)
        return 0
    msgs, info = scenarios.build(a.scenario, seed=a.seed, phone_id=a.id)
    if a.out:
        aidet.write_jsonl(a.out, msgs)
        print(f"wrote {len(msgs)} messages ({a.scenario}) -> {a.out}")
    if a.publish:
        c = mqtt_client(a.publish, f"phonesim-{a.id}")
        t_start, m0 = time.time(), msgs[0][0]
        print(f"publishing {a.scenario} as phone/{a.id}/… to {a.publish} "
              f"({(msgs[-1][0] - m0) / a.speed:.0f} s at {a.speed:g}x) — Ctrl-C to stop")
        try:
            for t, pid, topic, p in msgs:
                delay = (t - m0) / a.speed - (time.time() - t_start)
                if delay > 0:
                    time.sleep(delay)
                c.publish(f"phone/{pid}/{topic}", json.dumps(p), qos=0)
        except KeyboardInterrupt:
            pass
        c.loop_stop()
        c.disconnect()
    if not (a.out or a.publish):
        print(f"{a.scenario}: {len(msgs)} messages, {info} — add --out FILE or --publish HOST:PORT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
