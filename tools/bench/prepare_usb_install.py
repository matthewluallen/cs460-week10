#!/usr/bin/env python3
"""Offline verification and a reviewable USB-install plan; never execute the plan.

Use a complete repository checkout and the exact downloaded CI artifact directory.
``verify`` needs only Python's standard library. ``plan`` requires Python 3.10+
(the minimum for the pinned esptool 5.1.0), an explicitly chosen local port, and an
existing full 8 MiB backup. It prints JSON with argv arrays, never shell commands,
and does not import esptool/pyserial, open a port, create files, or run a process.

The local operator must establish device identity, security state, flash capacity,
load isolation, and download mode before using any emitted device command. Artifact
hashes establish consistency with the supplied manifest, not its authenticity.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import stat
import sys


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = ROOT / "qemu" / "generic-project-smoke.py"
ESPTOOL_VERSION = "5.1.0"
FLASH_SIZE = 0x800000
MANIFEST_LIMIT = 65536
FILE_LIMITS = {
    "firmware.elf": 64 * 1024 * 1024,
    "firmware.bin": 0x330000,
    "bootloader.bin": 0x8000,
    "partitions.bin": 0x1000,
    "boot_app0.bin": 0x2000,
}
EXPECTED_PARTITIONS = {
    "nvs": (1, 2, 0x9000, 0x5000),
    "otadata": (1, 0, 0xe000, 0x2000),
    "app0": (0, 0x10, 0x10000, 0x330000),
    "app1": (0, 0x11, 0x340000, 0x330000),
    "spiffs": (1, 0x82, 0x670000, 0x180000),
    "coredump": (1, 3, 0x7f0000, 0x10000),
}
SPARSE_WRITES = (
    ("bootloader.bin", 0),
    ("partitions.bin", 0x8000),
    ("boot_app0.bin", 0xe000),
    ("firmware.bin", 0x10000),
)
KEEP = ["--flash-mode", "keep", "--flash-freq", "keep", "--flash-size", "keep"]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def clean_text(value, label):
    value = str(value)
    require(value and not value.startswith("@") and not any(
        ord(char) < 32 or 127 <= ord(char) <= 159 for char in value
    ), f"{label}: empty values, @file arguments and control characters are not allowed")
    return value


def local_port(value):
    value = clean_text(value, "port")
    require("://" not in value and (
        (value.startswith("/dev/") and value != "/dev/") or
        re.fullmatch(r"(?:\\\\\.\\)?COM[1-9][0-9]*", value, re.I)
    ), "port: choose an exact local /dev/... or COM port; no URLs, wildcards or automatic selection")
    require(not any(char in value for char in "*?[]"), "port: wildcards are not allowed")
    return value


def source_commit(value):
    require(isinstance(value, str) and re.fullmatch(r"[0-9a-f]{40}", value),
            "source commit must be the expected full 40-character lowercase commit")
    return value


def regular_file(path, maximum, *, exact=None):
    path = Path(clean_text(path, "file path"))
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode), f"not a regular file (symlinks are not accepted): {path}")
    require(0 < info.st_size <= maximum, f"file size outside allowed bounds: {path.name}")
    if exact is not None:
        require(info.st_size == exact, f"{path.name}: expected exactly {exact} bytes")
    return path


def strict_json(text):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, f"duplicate manifest key: {key}")
            result[key] = value
        return result

    def invalid_constant(value):
        raise ValueError(f"non-finite manifest number: {value}")

    # The manifest has no floating-point fields; rejecting them avoids rounded
    # byte counts and overflowing values such as 1e9999.
    def invalid_float(value):
        raise ValueError(f"floating-point manifest number: {value}")

    return json.loads(text, object_pairs_hook=pairs, parse_constant=invalid_constant,
                      parse_float=invalid_float)


def artifact_validator():
    """Import definitions only; the QEMU runner's main guard is never entered."""
    require(VALIDATOR.is_file(), "complete checkout required: qemu/generic-project-smoke.py is missing")
    spec = importlib.util.spec_from_file_location("_usb_plan_twin_validator", VALIDATOR)
    require(spec is not None and spec.loader is not None, "cannot load shared artifact validator")
    module = importlib.util.module_from_spec(spec)
    # Compile the source directly so even the shared import produces no .pyc
    # output. This executes definitions only: its __name__ is not __main__.
    exec(compile(VALIDATOR.read_bytes(), str(VALIDATOR), "exec"), module.__dict__)
    require(module.FLASH_SIZE == FLASH_SIZE and set(module.FILES) == set(FILE_LIMITS),
            "shared artifact contract changed; review the USB plan before using it")
    return module


class ValidationSink:
    """Let the existing assembler validate its image without writing any file."""
    def __init__(self):
        self.partition_bytes = None
        self.application_header = None

    def write_bytes(self, image):
        require(self.partition_bytes is None and len(image) == FLASH_SIZE,
                "unexpected shared-validator output")
        self.partition_bytes = bytes(image[0x8000:0x9000])
        self.application_header = bytes(image[0x10000:0x10018])
        return len(image)


def partition_receipt(entries):
    return [{"name": name, "type": row[0], "subtype": row[1],
             "offset": row[2], "size": row[3]} for name, row in entries.items()]


def verify_artifacts(artifact_dir, expected_source, profile="secure"):
    expected_source = source_commit(expected_source)
    require(profile in ("secure", "vulnerable"), "profile must be secure or vulnerable")
    directory = Path(clean_text(artifact_dir, "artifact directory")).resolve(strict=True)
    require(directory.is_dir(), "artifact directory is not a directory")
    validator = artifact_validator()
    manifest_path = regular_file(directory / validator.MANIFEST, MANIFEST_LIMIT)
    manifest_bytes = manifest_path.read_bytes()
    manifest = strict_json(manifest_bytes.decode("utf-8"))
    require(isinstance(manifest, dict) and manifest.get("schema") == "tbd.generic-twin-artifact/v1",
            "unsupported artifact manifest")
    require(manifest.get("source_commit") == expected_source, "artifact/source commit mismatch")
    environment = "m5stamplc_secure" if profile == "secure" else "m5stamplc"
    require(manifest.get("environment") == environment and manifest.get("firmware_profile") == profile,
            "artifact profile/environment mismatch; vulnerable requires explicit selection")
    require(isinstance(manifest.get("elf_sha256"), str) and
            re.fullmatch(r"[0-9a-f]{64}", manifest["elf_sha256"]), "invalid manifest ELF hash")
    files = manifest.get("files")
    require(isinstance(files, dict) and set(files) == set(FILE_LIMITS),
            "artifact manifest must contain exactly the five retained image/ELF files")
    for name, limit in FILE_LIMITS.items():
        entry = files[name]
        require(isinstance(entry, dict) and set(entry) == {"size", "sha256"} and
                type(entry["size"]) is int and 0 < entry["size"] <= limit and
                isinstance(entry["sha256"], str) and re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]),
                f"invalid artifact manifest file entry: {name}")
        regular_file(directory / name, limit, exact=entry["size"])

    # Reuse, rather than copy, all hash/ELF/image/MD5 checks used by the exact
    # artifact twin. The assembler's output goes only to the in-memory sink.
    sink = ValidationSink()
    validator.assemble(directory, manifest, sink)
    entries = validator.partition_table(sink.partition_bytes)
    require(entries == EXPECTED_PARTITIONS,
            "unexpected full partition layout; all six default_8MB entries must match")
    require(sink.application_header[2:4] == b"\x02\x3f",
            "application header must declare DIO/8MB/80MHz; do not rewrite the artifact")
    return {
        "schema": "tbd.usb-artifact-verification/v1",
        "result": "verified_offline",
        "scope": "artifact integrity and expected layout; no device contact, boot result or provenance attestation",
        "artifact_dir": str(directory),
        "source_commit": expected_source,
        "environment": environment,
        "firmware_profile": profile,
        "elf_sha256": manifest["elf_sha256"],
        "manifest_sha256": digest(manifest_bytes),
        "validator": {"path": str(VALIDATOR), "sha256": digest(VALIDATOR.read_bytes())},
        "files": files,
        "chip": "esp32s3", "flash_size": FLASH_SIZE,
        "flash_mode": "dio", "flash_frequency_hz": 80000000,
        "partitions": partition_receipt(entries),
    }


def verify_backup(backup_file, verification):
    path = regular_file(backup_file, FLASH_SIZE, exact=FLASH_SIZE).resolve(strict=True)
    data = path.read_bytes()
    require(len(data) == FLASH_SIZE, "backup changed while reading; capture a stable full 8 MiB backup")
    directory = Path(verification["artifact_dir"])
    candidate = (directory / "partitions.bin").read_bytes()
    require(digest(candidate) == verification["files"]["partitions.bin"]["sha256"],
            "candidate partition file changed after verification")
    table = data[0x8000:0x8000 + len(candidate)]
    entries = artifact_validator().partition_table(table)
    require(entries == EXPECTED_PARTITIONS,
            "backup partition layout differs from candidate; do not move or overwrite saved state")
    require(table == candidate, "backup partition bytes differ from candidate; review layout before writing")
    nvs = entries["nvs"]
    return {
        "path": str(path), "size": len(data), "sha256": digest(data),
        "partition_sha256": digest(table), "partition_bytes_match": True,
        "partitions": partition_receipt(entries),
        "nvs": {"offset": nvs[2], "size": nvs[3],
                "sha256": digest(data[nvs[2]:nvs[2] + nvs[3]])},
        "scope": "local backup bytes only; target identity and unchanged contents require the prewrite device verification",
    }


def make_plan(artifact_dir, expected_source, port, backup_file, profile="secure"):
    require(sys.version_info >= (3, 10), "plan requires Python 3.10+ for esptool 5.1.0")
    port = local_port(port)
    verification = verify_artifacts(artifact_dir, expected_source, profile)
    backup = verify_backup(backup_file, verification)
    python = clean_text(sys.executable, "Python executable")
    tool = [python, "-m", "esptool"]
    device = tool + ["--chip", "esp32s3", "--port", port, "--baud", "460800",
                     "--before", "no-reset", "--after", "no-reset"]
    pairs = []
    writes = []
    for name, offset in SPARSE_WRITES:
        path = str(Path(verification["artifact_dir"]) / name)
        pairs.extend([hex(offset), path])
        entry = verification["files"][name]
        writes.append({"name": name, "path": path, "offset": offset, **entry,
                       "erased_sector_start": offset,
                       "erased_sector_end_exclusive": (offset + entry["size"] + 4095) // 4096 * 4096})
    backup_pair = ["0x0", backup["path"]]
    return {
        "schema": "tbd.usb-install-plan/v1", "result": "prepared_offline_not_executed",
        "artifact": verification, "backup": backup, "port": port,
        "tool": {"package": f"esptool=={ESPTOOL_VERSION}", "python_minimum": "3.10",
                 "version_argv": tool + ["version"], "required_version": ESPTOOL_VERSION},
        "execution": {"automatic": False, "argv_only": True, "shell": False,
                      "stop_on_any_failure": True, "revalidate_files_before_use": True},
        "local_prerequisites": [
            "Confirm this physical board and the exact selected port; re-enumeration requires a new plan.",
            "Keep output loads isolated and record both expansion-module labels and wiring.",
            "Retain the verified full backup privately; it can contain credentials and project settings.",
            "Confirm ESP32-S3 with 8 MiB flash and an unencrypted, unrestricted download path; stop if security state is unknown or protected.",
            "Close other serial clients and deliberately enter ROM download mode locally before these commands.",
            "Confirm esptool version 5.1.0 using the listed version argv; the helper does not install or execute it.",
            "Recheck local file hashes and stop after any failed command; never add --force or erase options.",
        ],
        "read_prerequisites": [
            {"id": "security_info", "argv": device + ["get-security-info"],
             "purpose": "Record device security state; an unknown/protected state blocks writing."},
            {"id": "flash_id", "argv": device + ["flash-id"],
             "purpose": "Confirm the intended device and 8 MiB flash; not a module-inventory report."},
        ],
        "steps": [
            {"id": "verify_unchanged_backup", "argv": device + ["verify-flash"] + KEEP + backup_pair,
             "purpose": "Before any write, require the entire target flash to match this existing full backup."},
            {"id": "write_sparse_candidate", "argv": device + ["write-flash"] + KEEP + pairs,
             "purpose": "Write only the four exact candidate files and their affected erase sectors."},
            {"id": "verify_sparse_candidate", "argv": device + ["verify-flash"] + KEEP + pairs,
             "purpose": "Verify the same bytes with the same unchanged header options; remain in download mode."},
        ],
        "writes": writes,
        "preserved_partitions": ["nvs", "app1", "spiffs", "coredump"],
        "preservation_status": "planned_exclusion_not_observed",
        "postinstall": {
            "automatic_reset": False,
            "mode_guaranteed": False,
            "reason": "NVS is preserved. A saved opt-in startup RUN project can execute on the next boot after its readiness checks.",
            "local_gate": "Before boot, extract NVS from the verified backup and verify-flash that exact region as described in the handoff; this offline plan produces no NVS preservation receipt. Review retained startup configuration before deliberate reboot. Observe normal console, then require PROGRAM and matching project json firmware_profile/ELF readback before commissioning; a plan is not a runtime receipt.",
            "expected_firmware_profile": profile, "expected_elf_sha256": verification["elf_sha256"],
        },
        "recovery": {
            "automatic": False, "requires_separate_local_decision": True,
            "scope": "Restore this same board's full flash only after confirming backup hash, identity and compatible security state; eFuses, RAM and external modules are not in the backup.",
            "steps": [
                {"id": "restore_full_backup", "argv": device + ["write-flash"] + KEEP + backup_pair},
                {"id": "verify_full_restore", "argv": device + ["verify-flash"] + KEEP + backup_pair},
            ],
            "automatic_reset": False,
        },
    }


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    commands = result.add_subparsers(dest="command", required=True)
    for name in ("verify", "plan"):
        command = commands.add_parser(name, help="offline artifact checks" if name == "verify" else "offline argv plan; requires existing full backup")
        command.add_argument("--artifact-dir", required=True)
        command.add_argument("--source-commit", required=True)
        command.add_argument("--profile", choices=("secure", "vulnerable"), default="secure")
        if name == "plan":
            command.add_argument("--port", required=True)
            command.add_argument("--backup-file", required=True)
    return result


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        if args.command == "verify":
            report = verify_artifacts(args.artifact_dir, args.source_commit, args.profile)
        else:
            report = make_plan(args.artifact_dir, args.source_commit, args.port, args.backup_file, args.profile)
        print(json.dumps(report, indent=2, ensure_ascii=True, allow_nan=False))
        return 0
    except (ValueError, RuntimeError, OSError, KeyError, TypeError, UnicodeError) as exc:
        print(f"USB install preparation stopped: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
