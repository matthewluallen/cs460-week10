# Your lab environment — what runs, what loads, and from where

This is a cybersecurity course, so the lab environment does not hide its own supply chain.
Everything below is auditable: the scripts named here are in the repo and readable, and the
background loader logs every fetch. If you can read an SBOM, you can read this.

## The idea: fast to Part 1, the rest loads in the open
The nine parts are sequential. Part 1 needs only the firmware **twin** and the **Logic Studio** —
small, quick. The heavy tools belong to later parts, so we don't block your start on them:
`./start.sh` gets you to Part 1 in seconds, and the later-part tools download **in the
background, visibly**, while you work. Watch them any time:

```
bash .devcontainer/prefetch.sh --status      # one-line status per tool
tail -f ~/.cache/cs460-prefetch/prefetch.log  # the live download log
./start.sh --info                             # lab map + tool + backload status
```

## What loads, when, and from where

**1. Baked into the prebuilt image** (pulled once, not built) — `.devcontainer/Dockerfile`,
pinned versions, pushed to `ghcr.io/matthewluallen/cs460-week10-devcontainer` by
`.github/workflows/devcontainer-image.yml`:
- Base: `mcr.microsoft.com/devcontainers/base:ubuntu-24.04`
- Node 20 (NodeSource), Python 3.12, GitHub CLI (official apt repos)
- bus/SAST tooling: `sigrok-cli`, `tshark`, `cppcheck`
- SBOM/CVE scanner **binaries**: `syft` v1.55.0, `grype` v0.121.0, `trivy` v0.58.0 (from anchore/aquasecurity)
- Python: `z3-solver`, `pymodbus`, `pyserial`
- QEMU twin runtime libs (`libglib/libpixman/libslirp/libgcrypt/libcap/libpng`)

**2. Linked/wired at container create** — `.devcontainer/postCreate.sh` (fast, repo-dependent):
links the baked twin into `qemu/twin/`, puts `pio` on PATH, makes the assessment libs importable.

**3. Fetched on demand when you start** — `./start.sh` → `qemu/fetch-qemu.sh` downloads the
**live** twin + flash image for your arch from the GitHub release `qemu-twin-v2`
(`qemu-twin-linux-<arch>.tar.xz`, ~4 MB). start.sh always fetches so you run the released twin,
not a stale baked copy.

**4. Backloaded in the background** — `.devcontainer/prefetch.sh` (spawned from
`postStartCommand`, logged, non-blocking):
| tool | needed by | from |
|---|---|---|
| `sigrok-cli` (if not baked) | Part 3 bus decode | Ubuntu apt |
| grype CVE database | Part 5 SBOM scan | anchore grype DB |
| PlatformIO ESP32-S3 platform + Xtensa toolchain | Part 9 secure build | PlatformIO registry |
| Playwright Chromium (optional) | Part 2 self-check | Playwright CDN |

Nothing in (4) is required: if a download hasn't finished when you reach its part, that part's
own script fetches it then, with its own visible progress. The loader only moves the wait off
your critical path — it never hides it.

## Auditing it yourself
- The twin's own provenance (source → binary) is in `qemu/twin-src/recon/README.md` and the
  `qemu-twin-v2` release notes (source commit + sha256 of each asset).
- Image contents are declared in `.devcontainer/Dockerfile` with pinned versions.
- Every background fetch is logged with a timestamp, reason, and source in the prefetch log.
