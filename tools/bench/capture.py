#!/usr/bin/env python3
"""Bounded, read-only normal-console evidence capture; never opens on import.

pyserial is optional until list-ports/capture is explicitly requested. This tool
makes no DTR/RTS/BREAK calls. Opening/closing a serial port can still change modem
lines in the library/OS/driver and can reset a board; see README.md.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys
import time


SCHEMA = "tbd.bench-capture/v1"
BAUD = 115200
READ_SLICE = 0.05
WRITE_TIMEOUT = 0.5
SETTLE = 0.15
PER_REPLY_BYTES = 32768
TOTAL_BYTES = 262144
TOTAL_SECONDS = 45.0
PROMPT = re.compile(rb"(?:\A|[\r\n])plc> \Z")
# Do not answer a startup menu, try a bootloader handshake, or leave gateway mode.
NON_CONSOLE = re.compile(
    rb"Serial Configuration|\[CONFIG\]|\[GW\]|waiting for download|DOWNLOAD_BOOT|"
    rb"(?:\A|[\r\n])(?:ESP-ROM:|rst:|Choose:|PASS:|SSID \(|static\? \(|IP: |GW: |MASK: )",
    re.IGNORECASE,
)
SENSITIVE = re.compile(
    rb'"(?:pass(?:word)?|passwd|cfg_pass|seed|token|secret|api_key|access_token|refresh_token|private_key|credentials?)"\s*:'
    rb'|(?:user pw|service pw|password|passwd|seed|token|secret|PASS)\s*[:=]',
    re.IGNORECASE,
)


@dataclass(frozen=True)
class Command:
    key: str
    text: str
    kind: str
    marker: str = ""


# Deliberately not a user-extensible shell. Every argument is a literal below.
COMMANDS = (
    Command("status_before", "status", "status"),
    Command("project_before", "project json", "json", "tbd.project-readback/v1"),
    Command("version", "ver", "version"),
    Command("program", "program", "program"),
    Command("inventory", "devices status", "json", "inventory"),
    Command("samples", "devices samples", "json", "samples"),
    Command("bindings", "bindings json", "json", "snapshot"),
    Command("port_c", "portc status", "json", "status"),
    Command("services", "proto json", "json", "svc"),
    Command("execution", "execution json", "json", "tbd.execution-readback/v1"),
    Command("status_after", "status", "status"),
    Command("project_after", "project json", "json", "tbd.project-readback/v1"),
)


@dataclass(frozen=True)
class Limits:
    observe_seconds: float = 2.0
    reply_seconds: float = 3.0
    total_seconds: float = TOTAL_SECONDS
    per_reply_bytes: int = PER_REPLY_BYTES
    total_bytes: int = TOTAL_BYTES


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def strict_json(text):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate JSON key")
            result[key] = value
        return result

    def invalid_constant(_):
        raise ValueError("non-finite JSON number")

    def finite_float(value):
        number = float(value)
        if not math.isfinite(number):
            raise ValueError("non-finite JSON number")
        return number

    return json.loads(text, object_pairs_hook=pairs, parse_constant=invalid_constant, parse_float=finite_float)


def read_window(stream, seconds, limit, deadline, clock, sleep, *, want_prompt):
    """Never flush/discard transport buffers or request unbounded reads."""
    data = bytearray()
    started = last_rx = clock()
    while True:
        now = clock()
        if NON_CONSOLE.search(data):
            return bytes(data), "endpoint_state_changed"
        if want_prompt and PROMPT.search(data) and now - last_rx >= SETTLE:
            return bytes(data), "prompt"
        if not want_prompt and now - last_rx >= SETTLE:
            return bytes(data), "quiet"
        if len(data) >= limit:
            return bytes(data), "byte_limit"
        if now >= deadline:
            return bytes(data), "total_deadline"
        if now - started >= seconds:
            return bytes(data), "timeout"
        try:
            chunk = stream.read(min(512, limit - len(data)))
        except KeyboardInterrupt:
            return bytes(data), "interrupted"
        except Exception as exc:
            return bytes(data), "read_error:" + type(exc).__name__
        if not isinstance(chunk, bytes) or len(chunk) > min(512, limit - len(data)):
            return bytes(data), "invalid_transport_read"
        if chunk:
            data.extend(chunk)
            last_rx = clock()
        else:
            # The real serial adapter already waits at most READ_SLICE; this also
            # bounds CPU use for a nonblocking adapter or deterministic fake.
            sleep(min(0.01, max(0.0, deadline - clock())))


def json_prefix(command, content):
    if command.key.startswith("project_") or command.key == "execution":
        return content.startswith(b'{"schema":"' + command.marker.encode() + b'"')
    fields = {"inventory": "devices", "samples": "devices", "bindings": "bindings", "port_c": "portc"}
    if command.key in fields:
        return content.startswith(('{' + json.dumps(fields[command.key]) + ':' + json.dumps(command.marker)).encode())
    return command.key == "services" and content.startswith(b'{"svc":')


def matches_json(command, value):
    if not isinstance(value, dict):
        return False
    if command.key.startswith("project_"):
        return value.get("schema") == command.marker and isinstance(value.get("installed"), bool)
    if command.key == "execution":
        return value.get("schema") == command.marker and isinstance(value.get("supported"), bool)
    if command.key in ("inventory", "samples"):
        return value.get("devices") == command.marker and isinstance(value.get("entries"), list)
    if command.key == "bindings":
        return value.get("bindings") == "snapshot" and isinstance(value.get("config"), dict) and isinstance(value.get("inputs"), list)
    if command.key == "port_c":
        return value.get("portc") == "status" and isinstance(value.get("active"), str)
    return command.key == "services" and isinstance(value.get("svc"), list)


def classify(command, received, termination):
    """Keep expected reply lines verbatim, never an indiscriminate port log.

    Initial/unsolicited output is not written to disk. Unexpected lines are
    counted and hashed by the caller; a mixed reply is never called complete.
    """
    result = {
        "command": command.text, "key": command.key,
        "termination": termination, "received_bytes": len(received),
        "received_sha256": digest(received), "status": "incomplete",
        "raw_kind": "recognized_reply_lines_verbatim", "raw_segments": [],
        "omitted_bytes": 0, "payload": None,
    }
    if SENSITIVE.search(received):
        result.update(status="withheld_sensitive_output", omitted_bytes=len(received))
        return result, b""
    stored = bytearray()
    offset = 0
    payloads, text_lines = [], []
    unsupported = error_reply = malformed = False
    unexpected = 0
    for line in received.splitlines(keepends=True):
        content = line.rstrip(b"\r\n")
        ignored = content in (b"", command.text.encode(), b"plc> ", b"plc> " + command.text.encode())
        keep = False
        if not ignored:
            if content in (b"? type 'help'", b"? unknown command", b"Unknown command. Type 'help' for list."):
                unsupported = keep = True
            elif content.startswith(b"usage: ") and content[7:].startswith(command.text.split()[0].encode()):
                unsupported = keep = True
            elif command.kind == "json" and content.startswith(b"{"):
                try:
                    value = strict_json(content.decode("utf-8"))
                except (ValueError, UnicodeError, RecursionError):
                    if json_prefix(command, content):
                        malformed = keep = True
                    else:
                        unexpected += 1
                else:
                    if matches_json(command, value):
                        payloads.append(value)
                        keep = True
                    elif isinstance(value, dict) and ("error" in value or value.get("bindings") == "error" or value.get("devices") == "error" or value.get("portc") == "error"):
                        error_reply = keep = True
                    else:
                        unexpected += 1
            elif command.kind == "status" and (
                re.fullmatch(rb"MODE=(PROGRAM|RUN|REMOTE)  scan last=\d+us min=\d+us max=\d+us  scans=\d+  hb=\d+", content)
                or re.fullmatch(rb"faults=\d+  forces=\d+ \(active=(YES|no)\)  IO-exp=(online|absent)  uptime=\d+s", content)
            ):
                keep = True
                text_lines.append(content)
            elif command.kind == "version" and re.fullmatch(rb"StamPLC teaching firmware[^\r\n]{0,200}", content):
                keep = True
                text_lines.append(content)
            elif command.kind == "program" and re.fullmatch(rb"\[PROGRAM\] (?:instr=\d+ mode=(?:PROGRAM|RUN|REMOTE) validation=(?:valid|invalid) scan_ms=\d+|D \d+ \d+ -?\d+ -?\d+|END)", content):
                keep = True
                text_lines.append(content)
            else:
                unexpected += 1
        if keep:
            result["raw_segments"].append({"received_offset": offset, "length": len(line), "file_offset": len(stored)})
            stored.extend(line)
        else:
            result["omitted_bytes"] += len(line)
        offset += len(line)
    result["unexpected_lines"] = unexpected
    framed = termination == "prompt"
    complete = False
    if command.kind == "json":
        complete = len(payloads) == 1
    elif command.kind == "status":
        complete = len(text_lines) == 2 and text_lines[0].startswith(b"MODE=") and text_lines[1].startswith(b"faults=")
    elif command.kind == "version":
        complete = len(text_lines) == 1
    elif command.kind == "program":
        if len(text_lines) >= 2 and text_lines[0].startswith(b"[PROGRAM] instr=") and text_lines[-1] == b"[PROGRAM] END":
            count = int(text_lines[0].split(b"instr=", 1)[1].split()[0])
            rows = text_lines[1:-1]
            complete = count <= 32 and len(rows) == count and all(row.startswith(f"[PROGRAM] D {i} ".encode()) for i, row in enumerate(rows))
    if unexpected or len(payloads) > 1 or (unsupported and (payloads or text_lines)) or (error_reply and (payloads or text_lines)):
        result["status"] = "mixed_reply"
    elif not framed or malformed:
        result["status"] = "timeout" if termination == "timeout" and not stored else "incomplete"
    elif unsupported:
        result["status"] = "unsupported"
    elif error_reply:
        result["status"] = "reported_error"
    elif complete:
        result["status"] = "complete"
        if payloads:
            result["payload"] = payloads[0]
    return result, bytes(stored)


class Capture:
    def __init__(self, limits=Limits(), clock=time.monotonic, sleep=time.sleep):
        self.limits, self.clock, self.sleep = limits, clock, sleep
        self.raw = {}
        self.report = {
            "schema": SCHEMA, "started_utc": utc_now(), "transport": "local_usb_serial",
            "baud": BAUD, "capture_scope": "initial_diagnostics_not_project_backup",
            "evidence_kind": "controller_reports_not_independent_hardware_measurements",
            "commands_planned": [c.text for c in COMMANDS], "results": [],
            "line_control": "no_explicit_DTR_RTS_or_BREAK_calls; library_OS_driver_open_close_may_change_lines",
            "limits": vars(limits), "completion": "not_started", "sent_commands": 0,
            "raw_policy": "only_recognized_reply_lines; unsolicited_or_sensitive_output_not_persisted",
            "unavailable": ["complete portable project, mappings and defaults", "independent physical module/contact identity", "calibration or electrical timing qualification"],
        }

    def finish(self, reason):
        self.report["completion"] = reason
        self.report["finished_utc"] = utc_now()
        before = next((r["payload"] for r in self.report["results"] if r["key"] == "project_before" and r["status"] == "complete"), None)
        after = next((r["payload"] for r in self.report["results"] if r["key"] == "project_after" and r["status"] == "complete"), None)
        keys = ("boot_id", "elf_sha256", "firmware_profile", "runtime_profile", "project_id", "payload_sha256", "generation")
        if before is not None and after is not None and all(k in before and k in after for k in keys):
            changed = [k for k in keys if before[k] != after[k]]
            self.report["identity_consistency"] = {"status": "changed" if changed else "reported_same", "changed_fields": changed, "basis": "start_end_controller_reports_not_attestation"}
            if changed:
                self.report["completion"] = "identity_changed"
        else:
            self.report["identity_consistency"] = {"status": "unavailable", "reason": "two complete identity reports with all fields required"}
            if reason == "complete":
                self.report["completion"] = "complete_with_gaps"
        return self.report

    def run(self, stream, *, console_ready=False):
        started = self.clock()
        deadline = started + self.limits.total_seconds
        total = 0
        raw, ending = read_window(stream, self.limits.observe_seconds, min(self.limits.per_reply_bytes, self.limits.total_bytes), deadline, self.clock, self.sleep, want_prompt=True)
        total += len(raw)
        observed = ending == "prompt"
        asserted = console_ready and not raw and ending == "timeout"
        self.report["readiness"] = {"basis": "observed_empty_prompt" if observed else "user_asserted_empty_normal_console" if asserted else "unknown", "termination": ending, "observed_bytes": len(raw), "observed_sha256": digest(raw), "raw_retained": False}
        if not (observed or asserted):
            return self.finish("normal_console_not_established")
        for index, command in enumerate(COMMANDS):
            if total >= self.limits.total_bytes or self.clock() >= deadline:
                return self.finish("total_budget_exhausted")
            # A reply to somebody else's request, a new startup banner, or any
            # queued partial input makes the command boundary uncertain. Abort.
            pending, boundary = read_window(stream, SETTLE + READ_SLICE, min(2048, self.limits.total_bytes - total), deadline, self.clock, self.sleep, want_prompt=False)
            total += len(pending)
            if pending or boundary != "quiet":
                self.report["boundary_event"] = {"before_command": command.text, "termination": boundary, "bytes": len(pending), "sha256": digest(pending), "raw_retained": False}
                return self.finish("console_boundary_changed")
            wire = (command.text + "\n").encode("ascii")
            sent_at = self.clock()
            try:
                count = stream.write(wire)
            except KeyboardInterrupt:
                return self.finish("interrupted_during_write")
            except Exception as exc:
                self.report["write_error"] = type(exc).__name__
                return self.finish("write_failed_no_retry")
            self.report["sent_commands"] += 1
            if count != len(wire):
                self.report["partial_write"] = {"command": command.text, "requested_bytes": len(wire), "reported_written_bytes": count}
                return self.finish("partial_write_no_retry")
            raw, ending = read_window(stream, self.limits.reply_seconds, min(self.limits.per_reply_bytes, self.limits.total_bytes - total), deadline, self.clock, self.sleep, want_prompt=True)
            total += len(raw)
            result, selected = classify(command, raw, ending)
            result["sent_elapsed_ms"] = round((sent_at - started) * 1000, 3)
            result["finished_elapsed_ms"] = round((self.clock() - started) * 1000, 3)
            result["raw_file"] = f"{index:02d}-{command.key}.reply.raw" if selected else None
            result["raw_bytes"] = len(selected)
            result["raw_sha256"] = digest(selected)
            if selected:
                self.raw[result["raw_file"]] = selected
            self.report["results"].append(result)
            self.report["received_bytes"] = total
            if result["status"] not in ("complete", "unsupported", "reported_error"):
                return self.finish("stopped_on_" + result["status"])
        return self.finish("complete" if all(r["status"] == "complete" for r in self.report["results"]) else "complete_with_gaps")

    def save(self, output):
        for filename, data in self.raw.items():
            with (output / filename).open("xb") as f:
                f.write(data)
        self.report["tool_sha256"] = digest(Path(__file__).read_bytes())
        with (output / "manifest.json").open("x", encoding="utf-8") as f:
            json.dump(self.report, f, indent=2, ensure_ascii=True, allow_nan=False)
            f.write("\n")


def open_serial(port, serial_module):
    kwargs = dict(port=None, baudrate=BAUD, bytesize=8, parity="N", stopbits=1,
                  timeout=READ_SLICE, write_timeout=WRITE_TIMEOUT,
                  xonxoff=False, rtscts=False, dsrdtr=False)
    if os.name == "posix":
        kwargs["exclusive"] = True
    stream = serial_module.Serial(**kwargs)
    stream.port = port
    # No dtr/rts setters, reset_input_buffer, send_break, flush or reset command.
    # Serial.open itself applies platform defaults; this cannot guarantee no reset.
    try:
        stream.open()
    except BaseException:
        stream.close()
        raise
    return stream


def local_port(value):
    if "://" in value or any(c in value for c in "\r\n\x00") or not (
        value.startswith("/dev/") or re.fullmatch(r"(?:\\\\\.\\)?COM[1-9][0-9]*", value, re.I)
    ):
        raise argparse.ArgumentTypeError("choose an explicit local /dev/... or COM port from list-ports; network URLs are not accepted")
    return value


def bounded_seconds(value):
    try:
        number = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("use seconds from 0.5 to 10") from exc
    if not math.isfinite(number) or not 0.5 <= number <= 10:
        raise argparse.ArgumentTypeError("use finite seconds from 0.5 to 10")
    return number


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="action", required=True)
    sub.add_parser("plan", help="print the exact allowlist; no serial import/open/send")
    sub.add_parser("list-ports", help="enumerate local ports only; never opens them")
    cap = sub.add_parser("capture", help="open only the chosen port and request bounded diagnostics")
    cap.add_argument("--port", required=True, type=local_port)
    cap.add_argument("--output", type=Path, required=True, help="new output directory; existing paths are never overwritten")
    cap.add_argument("--console-ready", action="store_true", help="assert you just observed this board's empty normal plc> prompt; only usable when passive observation receives no bytes")
    cap.add_argument("--observe-seconds", type=bounded_seconds, default=2.0)
    cap.add_argument("--reply-seconds", type=bounded_seconds, default=3.0)
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    if args.action == "plan":
        print("No port opened. Fixed read-only normal-console requests:")
        for command in COMMANDS:
            print("  " + command.text)
        print("Opening/closing a serial port may reset a board even without explicit DTR/RTS calls.")
        return 0
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        print("pyserial is required for this action. Install with: python -m pip install pyserial", file=sys.stderr)
        return 2
    if args.action == "list-ports":
        ports = sorted(list_ports.comports(), key=lambda p: p.device)
        print(json.dumps([{"port": p.device, "description": p.description, "vid": p.vid, "pid": p.pid} for p in ports], indent=2))
        return 0
    try:
        args.output.mkdir(parents=True, mode=0o700, exist_ok=False)
    except OSError as exc:
        print("Output directory must be new and writable; no port opened (" + type(exc).__name__ + ").", file=sys.stderr)
        return 2
    capture = Capture(Limits(observe_seconds=args.observe_seconds, reply_seconds=args.reply_seconds))
    capture.report["chosen_port"] = args.port
    print("Opening the explicitly chosen port. OS/driver/library open or close may reset the board; no reset-line calls are made.", file=sys.stderr)
    stream = None
    try:
        stream = open_serial(args.port, serial)
        capture.run(stream, console_ready=args.console_ready)
    except KeyboardInterrupt:
        capture.finish("interrupted")
    except Exception as exc:
        capture.report["transport_error"] = type(exc).__name__
        capture.finish("transport_failed")
    finally:
        if stream is not None:
            try:
                stream.close()
            except Exception as exc:
                capture.report["close_error"] = type(exc).__name__
    try:
        capture.save(args.output)
    except OSError as exc:
        print("Could not finish saving capture: " + type(exc).__name__, file=sys.stderr)
        return 2
    print(str(args.output / "manifest.json"))
    print("Capture: " + capture.report["completion"] + "; commands sent: " + str(capture.report["sent_commands"]))
    return 0 if capture.report["completion"] == "complete" else 2


if __name__ == "__main__":
    raise SystemExit(main())
