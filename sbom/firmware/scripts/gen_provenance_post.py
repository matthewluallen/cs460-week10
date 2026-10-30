"""POST extra script: regenerate the Logic-Studio provenance map from THIS build.

After the ELF is linked, run tools/gen-provenance-map.py with --elf <firmware.elf>
so web/public/firmware-provenance-map.json carries the current source file:lines
AND the current function + load address per event (re-derived from the ELF's
DWARF). This is what keeps the static<->dynamic map true across firmware changes:
every build refreshes it. Fails soft -- a problem here never breaks the build.
"""
import os
import subprocess
import sys

Import("env")  # noqa: F821  (PlatformIO/SCons injects this)

# repo layout: this file is sbom/firmware/scripts/ -> repo root is parents[3]
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
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
    cmd = [sys.executable, GEN, "--elf", elf]
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
            print("[provenance] map refreshed from", os.path.relpath(elf, REPO))
    except Exception as e:  # never break the build over the teaching map
        print("[provenance] non-fatal: could not refresh map:", e)


# Run after the ELF is produced.
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _regen)  # noqa: F821
