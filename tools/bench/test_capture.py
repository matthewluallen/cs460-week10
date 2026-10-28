"""Offline byte-stream tests only; no serial device, network or hardware proof."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

import capture


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakeStream:
    def __init__(self, clock, replies, initial=b"plc> ", fragment=512, short_write=False):
        self.clock, self.replies = clock, list(replies)
        self.pending = bytearray(initial)
        self.fragment, self.short_write = fragment, short_write
        self.writes = []
        self.read_sizes = []

    def read(self, size):
        self.read_sizes.append(size)
        self.clock.sleep(0.01)
        count = min(size, self.fragment, len(self.pending))
        result = bytes(self.pending[:count])
        del self.pending[:count]
        return result

    def write(self, data):
        self.writes.append(data)
        if self.short_write:
            return len(data) - 1
        if self.replies:
            reply = self.replies.pop(0)
            self.pending.extend(reply(data) if callable(reply) else reply)
        return len(data)


STATUS = b"MODE=PROGRAM  scan last=120us min=80us max=300us  scans=20  hb=20\r\nfaults=0  forces=0 (active=no)  IO-exp=absent  uptime=7s\r\n"


def project(profile="vulnerable", generation=1):
    return {
        "schema": "tbd.project-readback/v1", "installed": False,
        "status": "no_project", "mode": "PROGRAM", "boot_id": "a" * 32,
        "elf_sha256": "b" * 64, "firmware_profile": profile, "runtime_profile": profile,
        "project_id": "", "payload_sha256": "", "generation": generation,
        "modules_ready": False, "run_ready": False, "io_expansion": None,
    }


def payload(command, profile="vulnerable", generation=1):
    if command.kind == "status":
        return STATUS
    if command.key.startswith("project_"):
        value = project(profile, generation)
    elif command.key == "version":
        return b"StamPLC teaching firmware - PLC core v2 (mode/force/audit/console)\r\n"
    elif command.key == "program":
        return b"[PROGRAM] instr=1 mode=PROGRAM validation=valid scan_ms=50\r\n[PROGRAM] D 0 2 0 0\r\n[PROGRAM] END\r\n"
    elif command.key in ("inventory", "samples"):
        value = {"devices": command.marker, "schema": 1, "entries": [], "entry_count": 0, "generation": 1}
    elif command.key == "bindings":
        value = {"bindings": "snapshot", "schema": 1, "config": {"schema": 1, "inputs": []}, "inputs": []}
    elif command.key == "port_c":
        value = {"portc": "status", "active": "disabled", "saved": "disabled", "uart_ready": False}
    elif command.key == "services":
        value = {"svc": [], "netmax": 4, "neton": 0}
    else:
        value = {"schema": "tbd.execution-readback/v1", "supported": True, "boot_id": "a" * 32, "clock": "esp_timer_boot_us", "captured_us": 100, "runtime_backend": "custom-vm", "project_generation": 1, "mode": "PROGRAM", "timing": {}, "watchdog": {}, "first_failure": None}
    return json.dumps(value, separators=(",", ":")).encode() + b"\r\n"


def exchange(command, body=None):
    return command.text.encode() + b"\r\n" + (payload(command) if body is None else body) + b"plc> "


def run_capture(replies=None, **stream_options):
    clock = Clock()
    stream = FakeStream(clock, replies if replies is not None else [exchange(c) for c in capture.COMMANDS], **stream_options)
    cap = capture.Capture(clock=clock, sleep=clock.sleep)
    return cap, stream, clock


class CaptureTests(unittest.TestCase):
    def test_exact_allowlist_has_no_probe_write_or_secret_commands(self):
        self.assertEqual([c.text for c in capture.COMMANDS], [
            "status", "project json", "ver", "program", "devices status", "devices samples",
            "bindings json", "portc status", "proto json", "execution json", "status", "project json",
        ])

    def test_both_profiles_and_empty_inventory_are_reports_not_invented_hardware(self):
        for profile in ("vulnerable", "secure"):
            cap, stream, _ = run_capture([exchange(c, payload(c, profile)) for c in capture.COMMANDS])
            report = cap.run(stream)
            self.assertEqual(report["completion"], "complete")
            self.assertEqual(report["identity_consistency"]["status"], "reported_same")
            self.assertEqual(report["results"][1]["payload"]["firmware_profile"], profile)
            self.assertEqual(report["results"][4]["payload"]["entries"], [])
            self.assertEqual(stream.writes, [(c.text + "\n").encode() for c in capture.COMMANDS])
            self.assertNotIn("hardware_pass", report)

    def test_no_observed_prompt_sends_nothing(self):
        cap, stream, clock = run_capture(initial=b"")
        report = cap.run(stream)
        self.assertEqual(report["completion"], "normal_console_not_established")
        self.assertEqual(stream.writes, [])
        self.assertLess(clock(), 2.1)

    def test_explicit_empty_console_assertion_is_recorded(self):
        cap, stream, _ = run_capture(initial=b"")
        report = cap.run(stream, console_ready=True)
        self.assertEqual(report["completion"], "complete")
        self.assertEqual(report["readiness"]["basis"], "user_asserted_empty_normal_console")

    def test_config_bootloader_gateway_or_partial_input_cannot_be_overridden(self):
        for initial in (b"[CONFIG] Press ENTER within 5s for the config screen...\r\n", b"Choose: ", b"ESP-ROM:esp32s3\r\n", b"waiting for download\r\n", b"[GW] gateway mode\r\n", b"plc> set valve 1"):
            cap, stream, _ = run_capture(initial=initial)
            report = cap.run(stream, console_ready=True)
            self.assertEqual(report["completion"], "normal_console_not_established")
            self.assertEqual(stream.writes, [])

    def test_fragmented_echo_json_crlf_and_prompt_are_reassembled(self):
        cap, stream, _ = run_capture(fragment=3)
        report = cap.run(stream)
        self.assertEqual(report["completion"], "complete")
        self.assertEqual(cap.raw["01-project_before.reply.raw"], payload(capture.COMMANDS[1]))
        self.assertTrue(all(0 < n <= 512 for n in stream.read_sizes))

    def test_old_firmware_unsupported_is_a_gap_and_does_not_claim_identity(self):
        replies = [exchange(c, payload(c) if c.kind in ("status", "version") else b"? type 'help'\r\n") for c in capture.COMMANDS]
        cap, stream, _ = run_capture(replies)
        report = cap.run(stream)
        self.assertEqual(report["completion"], "complete_with_gaps")
        self.assertEqual(report["identity_consistency"]["status"], "unavailable")
        self.assertEqual(report["results"][1]["status"], "unsupported")
        self.assertEqual(report["results"][9]["status"], "unsupported")
        self.assertEqual(len(stream.writes), len(capture.COMMANDS))

    def test_partial_json_with_prompt_remains_incomplete_and_stops(self):
        partial = b'{"schema":"tbd.project-readback/v1","installed":'
        cap, stream, _ = run_capture([exchange(capture.COMMANDS[0]), exchange(capture.COMMANDS[1], partial + b"\r\n")])
        report = cap.run(stream)
        self.assertEqual(report["completion"], "stopped_on_incomplete")
        self.assertEqual(report["results"][-1]["payload"], None)
        self.assertEqual(cap.raw["01-project_before.reply.raw"], partial + b"\r\n")
        self.assertEqual(len(stream.writes), 2)

    def test_complete_json_without_prompt_is_not_a_complete_exchange(self):
        cap, stream, _ = run_capture([exchange(capture.COMMANDS[0]), payload(capture.COMMANDS[1])])
        report = cap.run(stream)
        self.assertEqual(report["results"][-1]["status"], "incomplete")
        self.assertIsNone(report["results"][-1]["payload"])
        self.assertEqual(len(stream.writes), 2)

    def test_no_reply_times_out_without_retries_or_later_commands(self):
        cap, stream, clock = run_capture([b""])
        report = cap.run(stream)
        self.assertEqual(report["completion"], "stopped_on_timeout")
        self.assertEqual(len(stream.writes), 1)
        self.assertLess(clock(), 3.5)

    def test_reply_mix_is_not_assigned_to_requested_schema(self):
        extra = b'{"devices":"samples","entries":[]}\r\n'
        cap, stream, _ = run_capture([exchange(capture.COMMANDS[0]), exchange(capture.COMMANDS[1], extra + payload(capture.COMMANDS[1]))])
        report = cap.run(stream)
        self.assertEqual(report["completion"], "stopped_on_mixed_reply")
        self.assertIsNone(report["results"][-1]["payload"])
        self.assertNotIn(extra, cap.raw["01-project_before.reply.raw"])

    def test_duplicate_expected_replies_are_ambiguous(self):
        command = capture.COMMANDS[1]
        result, _ = capture.classify(command, exchange(command, payload(command) * 2), "prompt")
        self.assertEqual(result["status"], "mixed_reply")
        self.assertIsNone(result["payload"])

    def test_duplicate_keys_and_non_finite_json_are_not_accepted(self):
        command = capture.COMMANDS[1]
        for body in (b'{"schema":"tbd.project-readback/v1","installed":true,"installed":false}', b'{"schema":"tbd.project-readback/v1","installed":true,"value":NaN}', b'{"schema":"tbd.project-readback/v1","installed":true,"value":1e9999}'):
            result, _ = capture.classify(command, exchange(command, body + b"\n"), "prompt")
            self.assertEqual(result["status"], "incomplete")
            self.assertIsNone(result["payload"])

    def test_sensitive_response_is_withheld_not_saved_as_raw(self):
        secret = b'UNIQUE_TEST_SECRET'
        body = b'{"schema":"tbd.project-readback/v1","installed":false,"seed":"' + secret + b'"}\n'
        cap, stream, _ = run_capture([exchange(capture.COMMANDS[0]), exchange(capture.COMMANDS[1], body)])
        report = cap.run(stream)
        self.assertEqual(report["completion"], "stopped_on_withheld_sensitive_output")
        self.assertNotIn(secret.decode(), json.dumps(report))
        self.assertNotIn(secret, b"".join(cap.raw.values()))

    def test_unsolicited_initial_text_not_retained(self):
        cap, stream, _ = run_capture(initial=b"old-session-private-text\r\nplc> ")
        report = cap.run(stream)
        self.assertEqual(report["completion"], "complete")
        self.assertNotIn("old-session-private-text", json.dumps(report))
        self.assertNotIn(b"old-session-private-text", b"".join(cap.raw.values()))

    def test_short_write_is_not_retried(self):
        cap, stream, _ = run_capture(short_write=True)
        report = cap.run(stream)
        self.assertEqual(report["completion"], "partial_write_no_retry")
        self.assertEqual(len(stream.writes), 1)
        self.assertEqual(report["results"], [])

    def test_byte_budget_stops_at_exact_bound(self):
        cap, stream, _ = run_capture([b"x" * (capture.PER_REPLY_BYTES * 2)])
        report = cap.run(stream)
        self.assertEqual(report["results"][0]["termination"], "byte_limit")
        self.assertEqual(report["results"][0]["received_bytes"], capture.PER_REPLY_BYTES)
        self.assertEqual(len(stream.writes), 1)

    def test_generation_change_is_not_combined_into_one_identity(self):
        replies = [exchange(c, payload(c, generation=2 if c.key == "project_after" else 1)) for c in capture.COMMANDS]
        cap, stream, _ = run_capture(replies)
        report = cap.run(stream)
        self.assertEqual(report["completion"], "identity_changed")
        self.assertEqual(report["identity_consistency"]["changed_fields"], ["generation"])

    def test_missing_identity_fields_are_reported_as_a_gap(self):
        replies = []
        for command in capture.COMMANDS:
            body = payload(command)
            if command.key.startswith("project_"):
                value = json.loads(body)
                del value["elf_sha256"]
                body = json.dumps(value).encode() + b"\n"
            replies.append(exchange(command, body))
        cap, stream, _ = run_capture(replies)
        report = cap.run(stream)
        self.assertEqual(report["completion"], "complete_with_gaps")
        self.assertEqual(report["identity_consistency"]["status"], "unavailable")

    def test_total_deadline_cannot_be_extended_by_slow_replies(self):
        cap, stream, clock = run_capture()
        cap.limits = capture.Limits(total_seconds=0.7)
        report = cap.run(stream)
        self.assertLess(clock(), 0.8)
        self.assertLess(len(stream.writes), len(capture.COMMANDS))
        self.assertNotEqual(report["completion"], "complete")

    def test_endpoint_reset_during_reply_stops_without_followup(self):
        cap, stream, _ = run_capture([b"ESP-ROM:esp32s3\r\n"])
        report = cap.run(stream)
        self.assertEqual(report["results"][0]["termination"], "endpoint_state_changed")
        self.assertEqual(len(stream.writes), 1)
        self.assertFalse(cap.raw)

    def test_read_failure_retains_prior_reply_and_stops(self):
        cap, stream, _ = run_capture()
        original_read = stream.read

        def read(size):
            if len(stream.writes) == 2:
                raise OSError("test-only transport failure")
            return original_read(size)

        stream.read = read
        report = cap.run(stream)
        self.assertEqual(report["results"][0]["status"], "complete")
        self.assertEqual(report["results"][1]["termination"], "read_error:OSError")
        self.assertEqual(len(stream.writes), 2)

    def test_save_preserves_exact_reply_bytes_and_never_overwrites(self):
        cap, stream, _ = run_capture()
        cap.run(stream)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            cap.save(output)
            report = json.loads((output / "manifest.json").read_text())
            for item in report["results"]:
                raw = (output / item["raw_file"]).read_bytes()
                self.assertEqual(capture.digest(raw), item["raw_sha256"])
                self.assertEqual(len(raw), item["raw_bytes"])
            with self.assertRaises(FileExistsError):
                cap.save(output)

    def test_adapter_does_not_call_line_controls_or_flush(self):
        calls = []

        class Port:
            def __init__(self, **kwargs):
                calls.append(("construct", kwargs))

            def __setattr__(self, name, value):
                if name != "port":
                    raise AssertionError("unexpected serial property " + name)
                calls.append(("port", value))

            def open(self):
                calls.append(("open",))

            def close(self):
                calls.append(("close",))

        stream = capture.open_serial("COM9", types.SimpleNamespace(Serial=Port))
        stream.close()
        self.assertEqual([c[0] for c in calls], ["construct", "port", "open", "close"])
        self.assertEqual(calls[0][1]["port"], None)
        self.assertEqual(calls[0][1]["timeout"], capture.READ_SLICE)
        self.assertEqual(calls[0][1]["write_timeout"], capture.WRITE_TIMEOUT)
        self.assertFalse(calls[0][1]["rtscts"])
        self.assertFalse(calls[0][1]["dsrdtr"])

    def test_plan_and_list_ports_never_open_or_send(self):
        serial = types.ModuleType("serial")
        serial.Serial = lambda **kwargs: self.fail("port unexpectedly opened")
        tools = types.ModuleType("serial.tools")
        tools.list_ports = types.SimpleNamespace(comports=lambda: [types.SimpleNamespace(device="COM9", description="fixture", vid=1, pid=2)])
        with patch.dict(sys.modules, {"serial": serial, "serial.tools": tools}), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(capture.main(["plan"]), 0)
            self.assertEqual(capture.main(["list-ports"]), 0)

    def test_capture_requires_explicit_port_and_new_output(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            capture.main(["capture", "--output", "unused"])
        for value in ("socket://host:23", "rfc2217://host:23", "auto", "COM1\nmode run"):
            with self.assertRaises(Exception):
                capture.local_port(value)
        for value in ("/dev/cu.usbmodem123", "/dev/ttyACM0", "COM9", r"\\.\COM12"):
            self.assertEqual(capture.local_port(value), value)
        for value in ("nan", "inf", "-1", "999"):
            with self.assertRaises(Exception):
                capture.bounded_seconds(value)

    def test_existing_output_directory_prevents_open(self):
        serial = types.ModuleType("serial")
        serial.Serial = lambda **kwargs: self.fail("port unexpectedly opened")
        tools = types.ModuleType("serial.tools")
        tools.list_ports = types.SimpleNamespace(comports=lambda: [])
        with tempfile.TemporaryDirectory() as directory, patch.dict(sys.modules, {"serial": serial, "serial.tools": tools}), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(capture.main(["capture", "--port", "COM9", "--output", directory]), 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
