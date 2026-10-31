"""POST extra script: generate the Logic-Studio provenance map for THIS build.

After the ELF is linked, run tools/gen-provenance-map.py with --elf <firmware.elf>.
It writes web/public/provenance-maps/<ELF SHA-256>.json (+ index.json): the current
source file:lines AND the function + load address per event, re-derived from the
ELF's DWARF, keyed by the same ELF SHA-256 the image carries in its app descriptor.
Provenance Studio picks the map whose hash equals the RUNNING twin's (GET
/twin/identity), so building the secure profile no longer overwrites the committed
map that matches the vulnerable twin. Fails soft -- never breaks the build.
"""
import os
import subprocess
import sys

Import("env")  # noqa: F821  (PlatformIO/SCons injects this)

# PlatformIO executes extra scripts without a module-level __file__, so derive the
# repo root from the build environment instead of __file__ (using __file__ here
# raises NameError and FAILS the whole build). PROJECT_DIR is sbom/firmware; the
# repo root is two levels up.
REPO = os.path.abspath(os.path.join(env.subst("$PROJECT_DIR"), "..", ".."))  # noqa: F821
GEN = os.path.join(REPO, "tools", "gen-provenance-map.py")


def _tool(suffix):
    """Derive an xtensa cross tool from the build's $CC (…-gcc -> …-<suffix>)."""
    try:
        cc = env.subst("$CC")  # noqa: F821
        if cc.endswith("-gcc"):
            cand = cc[:-4] + "-" + suffix
            if os.path.exists(cand):
                return cand
    except Exception:
        pass
    return None  # let the generator fall back to PATH / ~/.platformio


def _regen(*args, **kwargs):
    elf = os.path.join(env.subst("$BUILD_DIR"), env.subst("$PROGNAME") + ".elf")  # noqa: F821
    if not os.path.exists(GEN):
        print("[provenance] generator not found, skipping:", GEN)
        return
    cmd = [sys.executable, GEN, "--elf", elf, "--profile", env.subst("$PIOENV")]  # noqa: F821
    od, a2l = _tool("objdump"), _tool("addr2line")
    if od:
        cmd += ["--objdump", od]
    if a2l:
        cmd += ["--addr2line", a2l]
    try:
        r = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, timeout=180)
        sys.stdout.write(r.stdout)
        if r.returncode != 0:
            print("[provenance] non-fatal: generator exited", r.returncode)
            sys.stderr.write(r.stderr)
        else:
            print("[provenance] per-build map written for", os.path.relpath(elf, REPO))
    except Exception as e:  # never break the build over the teaching map
        print("[provenance] non-fatal: could not refresh map:", e)


# Run after the ELF is produced.
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _regen)  # noqa: F821
