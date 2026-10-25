"""Version-checked M5StamPLC 1.2.0 compatibility with M5Unified 0.2.21.

The old bench cache already contained these five boolean-to-enum corrections. Also preload OFF before enabling
relay output direction, so the old output latch cannot briefly drive a relay.
Run after PlatformIO constructs dependencies, before compilation. Refuse any
other source content rather than silently patching an unknown library version.
"""
from pathlib import Path
import hashlib

Import("env")

root = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "M5StamPLC"
patches = [
    ("src/M5StamPLC.cpp",
     "d3251015939ea5416a05d8a5d7cba00a607abade0ab3e308ae881f94797139b2",
     "662385898227bcf34ddbc50af1a8b1e7d9ceaf8ab3d4a7281fa85a6a2b97e27e",
     [(f"setPullMode({pin}, false)", f"setPullMode({pin}, m5::IOExpander_Base::pull_none)")
      for pin in (4, 5, 6)] + [('            _io_expander_b->pinMode(i, AW9523_Class::AW_OUTPUT);\r\n            _io_expander_b->digitalWrite(i, false);', '            _io_expander_b->digitalWrite(i, false);\r\n            _io_expander_b->pinMode(i, AW9523_Class::AW_OUTPUT);')]),
    ("src/modules/M5StamPLC_AC.cpp",
     "83157d41938549c62359c2a54a82f312304b2f21a63fadf03ecdc4722e80d688",
     "604158c2f39fee44430e5e5e0455a95ebe1d58c4ed6b5138504f977e1f5ba36e",
     [("setPullMode(_pin_relay, false)", "setPullMode(_pin_relay, m5::IOExpander_Base::pull_none)"),
      ("setPullMode(pin, true)", "setPullMode(pin, m5::IOExpander_Base::pull_up)")]),
]
for name, original, patched, replacements in patches:
    path = root / name
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest == patched:
        continue
    # Also accept the previously pinned enum-only correction during upgrades.
    if name == "src/M5StamPLC.cpp" and digest == "a303d28749fad7e209012ee6b799f24a6bc4088614d04d025182b60eff70e064":
        replacements = replacements[-1:]
    elif digest != original:
        raise RuntimeError(f"Unrecognized M5StamPLC compatibility source: {name}")
    for before, after in replacements:
        assert data.count(before.encode()) == 1
        data = data.replace(before.encode(), after.encode())
    assert hashlib.sha256(data).hexdigest() == patched
    path.write_bytes(data)
    print(f"TBD: applied pinned M5StamPLC pull-mode compatibility: {name}")
