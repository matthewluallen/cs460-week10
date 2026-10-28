"""Offline synthetic-image tests; no serial access or claim of bootable fixtures."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

import prepare_usb_install as prepare


SOURCE = "a" * 40


def table_bytes(entries=None):
    entries = prepare.EXPECTED_PARTITIONS if entries is None else entries
    data = b"".join(struct.pack("<HBBII16sI", 0x50aa, kind, subtype, offset, size,
                                name.encode("ascii"), 0)
                    for name, (kind, subtype, offset, size) in entries.items())
    data += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(data).digest()
    return data + b"\xff" * (3072 - len(data))


def write_fixture(directory, profile="secure"):
    directory.mkdir()
    elf = b"\x7fELF" + b"synthetic retained ELF identity, not a runnable application" * 3
    boot = bytearray(512)
    boot[:4] = b"\xe9\x03\x02\x3f"
    struct.pack_into("<H", boot, 12, 9)
    app = bytearray(1024)
    app[:4] = b"\xe9\x06\x02\x3f"
    struct.pack_into("<H", app, 12, 9)
    struct.pack_into("<I", app, 32, 0xabcd5432)
    app[176:208] = hashlib.sha256(elf).digest()
    files = {"firmware.elf": elf, "firmware.bin": bytes(app), "bootloader.bin": bytes(boot),
             "partitions.bin": table_bytes(), "boot_app0.bin": b"\xff" * 8192}
    for name, value in files.items():
        (directory / name).write_bytes(value)
    manifest = {
        "schema": "tbd.generic-twin-artifact/v1", "source_commit": SOURCE,
        "environment": "m5stamplc_secure" if profile == "secure" else "m5stamplc",
        "firmware_profile": profile, "elf_sha256": prepare.digest(elf),
        "files": {name: {"size": len(value), "sha256": prepare.digest(value)}
                  for name, value in files.items()},
        "validation": "synthetic test fixture only",
    }
    (directory / "twin-artifact.json").write_text(json.dumps(manifest), encoding="utf-8")
    return manifest


def backup_fixture(path, artifact_dir):
    # Non-FF NVS exercises digesting/preservation without treating blank state as
    # a prerequisite or claiming the existing controller has no saved project.
    data = bytearray(b"\xff") * prepare.FLASH_SIZE
    data[0x9000:0xe000] = bytes(range(256)) * 80
    for name, offset in prepare.SPARSE_WRITES:
        value = (artifact_dir / name).read_bytes()
        data[offset:offset + len(value)] = value
    path.write_bytes(data)
    return bytes(data)


class PrepareTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.artifacts = self.root / "candidate with spaces"
        self.manifest = write_fixture(self.artifacts)
        self.backup = self.root / "private backup.bin"
        self.backup_data = backup_fixture(self.backup, self.artifacts)

    def write_manifest(self):
        (self.artifacts / "twin-artifact.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def replace(self, name, data):
        (self.artifacts / name).write_bytes(data)
        self.manifest["files"][name] = {"size": len(data), "sha256": prepare.digest(data)}
        self.write_manifest()

    def verify(self, **kwargs):
        return prepare.verify_artifacts(self.artifacts, SOURCE, **kwargs)

    def plan(self, **kwargs):
        return prepare.make_plan(self.artifacts, SOURCE, "/dev/ttyACM0", self.backup, **kwargs)

    def test_both_explicit_profiles_use_shared_validator_and_exact_six_partitions(self):
        for profile in ("secure", "vulnerable"):
            with self.subTest(profile=profile):
                directory = self.root / profile
                manifest = write_fixture(directory, profile)
                report = prepare.verify_artifacts(directory, SOURCE, profile)
                self.assertEqual(report["result"], "verified_offline")
                self.assertEqual(report["firmware_profile"], profile)
                self.assertEqual(report["elf_sha256"], manifest["elf_sha256"])
                self.assertEqual([row["name"] for row in report["partitions"]], list(prepare.EXPECTED_PARTITIONS))
                self.assertEqual(report["validator"]["sha256"], prepare.digest(prepare.VALIDATOR.read_bytes()))

    def test_secure_default_never_accepts_teaching_manifest_implicitly(self):
        self.manifest.update(environment="m5stamplc", firmware_profile="vulnerable")
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "profile/environment"):
            self.verify()
        self.assertEqual(self.verify(profile="vulnerable")["firmware_profile"], "vulnerable")
        self.manifest["environment"] = "m5stamplc_secure"
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "profile/environment"):
            self.verify(profile="vulnerable")

    def test_expected_full_source_is_required_and_foreign_schema_rejected(self):
        for source in (SOURCE[:7], "A" * 40, "b" * 40, "", None):
            with self.subTest(source=source), self.assertRaises(ValueError):
                prepare.verify_artifacts(self.artifacts, source)
        self.manifest["schema"] = "foreign/artifact"
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "unsupported artifact"):
            self.verify()

    def test_each_retained_file_hash_is_checked_by_shared_assembler(self):
        for name in prepare.FILE_LIMITS:
            original = (self.artifacts / name).read_bytes()
            data = bytearray(original)
            # Alter non-header data so this tests integrity rather than format.
            data[-1] ^= 1
            (self.artifacts / name).write_bytes(data)
            with self.subTest(name=name), self.assertRaises((RuntimeError, ValueError)):
                self.verify()
            (self.artifacts / name).write_bytes(original)

    def test_elf_descriptor_binding_cannot_be_replaced_with_consistent_manifest_hashes(self):
        elf = (self.artifacts / "firmware.elf").read_bytes() + b"different ELF"
        self.replace("firmware.elf", elf)
        self.manifest["elf_sha256"] = prepare.digest(elf)
        self.write_manifest()
        with self.assertRaisesRegex(RuntimeError, "descriptor.*ELF"):
            self.verify()

    def test_both_images_require_s3_dio_80mhz_8mb_even_after_rehashing(self):
        for name in ("bootloader.bin", "firmware.bin"):
            original = (self.artifacts / name).read_bytes()
            for index, value in ((0, 0), (2, 0), (3, 0x2f), (3, 0x30), (12, 0)):
                with self.subTest(name=name, index=index, value=value):
                    data = bytearray(original)
                    data[index] = value
                    self.replace(name, bytes(data))
                    with self.assertRaises((ValueError, RuntimeError)):
                        self.verify()
                    self.replace(name, original)

    def test_partition_md5_and_exact_tail_partitions_are_required(self):
        original = (self.artifacts / "partitions.bin").read_bytes()
        data = bytearray(original)
        data[6 * 32 + 16] ^= 1
        self.replace("partitions.bin", bytes(data))
        with self.assertRaisesRegex(RuntimeError, "MD5"):
            self.verify()
        cases = [dict(list(prepare.EXPECTED_PARTITIONS.items())[:4]),
                 {**prepare.EXPECTED_PARTITIONS, "coredump": (1, 3, 0x7f0000, 0x9000)},
                 {**prepare.EXPECTED_PARTITIONS, "spiffs": (1, 0x83, 0x670000, 0x180000)}]
        for entries in cases:
            with self.subTest(entries=entries):
                self.replace("partitions.bin", table_bytes(entries))
                with self.assertRaisesRegex(ValueError, "full partition layout"):
                    self.verify()

    def test_manifest_shapes_duplicates_nonfinite_and_extra_files_fail_closed(self):
        path = self.artifacts / "twin-artifact.json"
        original = path.read_text()
        for text in (original[:-1] + ', "schema":"tbd.generic-twin-artifact/v1"}',
                     original.replace('"size": 1024', '"size": 1024.0'),
                     original.replace('"size": 1024', '"size": 1e9999'),
                     original.replace('"size": 1024', '"size": NaN'),
                     original.replace('"size": 1024', '"size": true')):
            with self.subTest(text=text[-80:]):
                path.write_text(text)
                with self.assertRaises(ValueError):
                    self.verify()
        self.manifest["files"]["surprise.bin"] = {"size": 1, "sha256": "0" * 64}
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "exactly the five"):
            self.verify()

    def test_missing_oversized_and_symlink_artifacts_are_rejected(self):
        path = self.artifacts / "firmware.elf"
        path.unlink()
        with self.assertRaises(OSError):
            self.verify()
        path.symlink_to(self.backup)
        with self.assertRaisesRegex(ValueError, "regular file"):
            self.verify()
        path.unlink()
        path.write_bytes(b"\x7fELF")
        self.manifest["files"]["firmware.elf"]["size"] = prepare.FILE_LIMITS["firmware.elf"] + 1
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "file entry"):
            self.verify()

    def test_full_existing_backup_is_hashed_including_nonblank_nvs(self):
        report = self.plan()
        backup = report["backup"]
        self.assertEqual(backup["size"], 0x800000)
        self.assertEqual(backup["sha256"], prepare.digest(self.backup_data))
        self.assertEqual(backup["nvs"], {"offset": 0x9000, "size": 0x5000,
                         "sha256": prepare.digest(self.backup_data[0x9000:0xe000])})
        self.assertTrue(backup["partition_bytes_match"])
        self.assertNotIn("nvs_bytes", json.dumps(report))
        self.assertFalse(report["postinstall"]["mode_guaranteed"])
        self.assertIn("saved opt-in startup RUN", report["postinstall"]["reason"])
        self.assertIn("no NVS preservation receipt", report["postinstall"]["local_gate"])
        self.assertEqual(report["preservation_status"], "planned_exclusion_not_observed")

    def test_backup_missing_short_long_symlink_and_invalid_md5_block_plan(self):
        for size in (16, 0x7fffff, 0x800001):
            with self.subTest(size=size):
                self.backup.write_bytes(b"\xff" * size)
                with self.assertRaises(ValueError):
                    self.plan()
        self.backup.unlink()
        with self.assertRaises(OSError):
            self.plan()
        self.backup.symlink_to(self.artifacts / "firmware.bin")
        with self.assertRaisesRegex(ValueError, "regular file"):
            self.plan()
        self.backup.unlink()
        data = bytearray(self.backup_data)
        data[0x8000 + 6 * 32 + 16] ^= 1
        self.backup.write_bytes(data)
        with self.assertRaisesRegex(RuntimeError, "MD5"):
            self.plan()

    def test_changed_backup_layout_or_nvs_location_blocks_plan(self):
        cases = [{**prepare.EXPECTED_PARTITIONS, "nvs": (1, 2, 0xa000, 0x4000)},
                 {**prepare.EXPECTED_PARTITIONS, "spiffs": (1, 0x83, 0x670000, 0x180000)}]
        for entries in cases:
            with self.subTest(entries=entries):
                data = bytearray(self.backup_data)
                data[0x8000:0x8c00] = table_bytes(entries)
                self.backup.write_bytes(data)
                with self.assertRaises((RuntimeError, ValueError)):
                    self.plan()

    def test_logically_equal_but_different_partition_bytes_require_review(self):
        data = bytearray(self.backup_data)
        table = bytearray(table_bytes())
        # Bytes after NUL in a label do not change the shared logical table.
        table[12 + len("nvs") + 1] = 1
        table[6 * 32 + 16:7 * 32] = hashlib.md5(table[:6 * 32]).digest()
        data[0x8000:0x8c00] = table
        self.backup.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "partition bytes differ"):
            self.plan()

    def test_command_order_fixed_port_sparse_offsets_keep_and_no_reset(self):
        report = self.plan()
        self.assertEqual(report["tool"]["package"], "esptool==5.1.0")
        self.assertEqual([step["id"] for step in report["steps"]],
                         ["verify_unchanged_backup", "write_sparse_candidate", "verify_sparse_candidate"])
        self.assertEqual([entry["offset"] for entry in report["writes"]], [0, 0x8000, 0xe000, 0x10000])
        self.assertEqual(report["preserved_partitions"], ["nvs", "app1", "spiffs", "coredump"])
        steps = report["steps"] + report["read_prerequisites"] + report["recovery"]["steps"]
        for step in steps:
            args = step["argv"]
            self.assertEqual(args[:3], [sys.executable, "-m", "esptool"])
            self.assertEqual(args[args.index("--port") + 1], "/dev/ttyACM0")
            self.assertEqual(args[args.index("--baud") + 1], "460800")
            self.assertEqual(args[args.index("--before") + 1], "no-reset")
            self.assertEqual(args[args.index("--after") + 1], "no-reset")
            self.assertFalse(set(args) & {"--force", "--erase-all", "erase-flash", "erase-region", "run", "--diff"})
            if "write-flash" in args or "verify-flash" in args:
                for flag in ("--flash-mode", "--flash-freq", "--flash-size"):
                    self.assertEqual(args[args.index(flag) + 1], "keep")
        first = report["steps"][0]["argv"]
        self.assertEqual(first[-2:], ["0x0", str(self.backup.resolve())])
        write = report["steps"][1]["argv"]
        verify = report["steps"][2]["argv"]
        self.assertEqual(write[write.index("write-flash") + 1:], verify[verify.index("verify-flash") + 1:])
        for item in report["writes"]:
            self.assertFalse(item["erased_sector_start"] < 0xe000 and item["erased_sector_end_exclusive"] > 0x9000)
            self.assertFalse(item["erased_sector_start"] < 0x670000 and item["erased_sector_end_exclusive"] > 0x340000)

    def test_explicit_windows_and_posix_ports_and_hostile_paths(self):
        for port in ("COM3", "com20", r"\\.\COM12", "/dev/cu.usbmodem123", "/dev/serial/by-id/usb-123"):
            self.assertEqual(prepare.local_port(port), port)
        for port in ("", "auto", "@opts", "/dev/", "COM0", "socket://localhost:5555", "rfc2217://board", "/dev/tty*", "COM3\n", "/dev/tty\x00x", "/dev/tty\x7fx"):
            with self.subTest(port=port), self.assertRaises(ValueError):
                prepare.local_port(port)
        for path in ("@file", "dir\nfile", "dir\x1bfile", "dir\x80file"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                prepare.clean_text(path, "path")

    def test_missing_shared_validator_and_unsupported_python_fail_explicitly(self):
        with patch.object(prepare, "VALIDATOR", self.root / "missing.py"):
            with self.assertRaisesRegex(ValueError, "complete checkout"):
                self.verify()
        with patch.object(prepare.sys, "version_info", (3, 9, 20)):
            with self.assertRaisesRegex(ValueError, "Python 3.10"):
                self.plan()

    def test_offline_cli_never_opens_device_executes_process_or_changes_files(self):
        before = {str(p.relative_to(self.root)): prepare.digest(p.read_bytes())
                  for p in self.root.rglob("*") if p.is_file()}
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch("subprocess.Popen", side_effect=AssertionError("process forbidden")), \
             patch("subprocess.run", side_effect=AssertionError("process forbidden")), \
             patch("socket.socket", side_effect=AssertionError("socket forbidden")), \
             patch.dict(sys.modules, {"serial": None, "esptool": None}), \
             contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = prepare.main(["plan", "--artifact-dir", str(self.artifacts), "--source-commit", SOURCE,
                                 "--port", "COM3", "--backup-file", str(self.backup)])
        self.assertEqual(code, 0, stderr.getvalue())
        self.assertEqual(json.loads(stdout.getvalue())["port"], "COM3")
        after = {str(p.relative_to(self.root)): prepare.digest(p.read_bytes())
                 for p in self.root.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_cli_requires_port_and_backup_and_reports_errors_without_partial_plan(self):
        for options in ([], ["--port", "COM3"], ["--backup-file", str(self.backup)]):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
                prepare.main(["plan", "--artifact-dir", str(self.artifacts), "--source-commit", SOURCE] + options)
            self.assertEqual(raised.exception.code, 2)
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = prepare.main(["verify", "--artifact-dir", str(self.artifacts), "--source-commit", "bad"])
        self.assertEqual(code, 2)
        self.assertEqual(stdout.getvalue(), "")
        self.assertIn("preparation stopped", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
