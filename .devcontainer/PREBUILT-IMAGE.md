# Prebuilt devcontainer image (fast setup for Codespaces **and** local VS Code)

The lab's first-time environment build runs a ~10-minute `postCreate.sh` (PlatformIO
toolchain + warm firmware build, Playwright Chromium, SBOM/scanner tools, the QEMU twin).
To make startup fast on **both** GitHub Codespaces and local VS Code Dev Containers, those
heavy steps are baked into a prebuilt image published to GHCR.

## Pieces
- **`.devcontainer/Dockerfile`** — bakes every heavy step into image layers (tools,
  Node/Python/gh, PlatformIO + ESP32-S3 toolchain, Playwright Chromium at `/ms-playwright`,
  and the v2 twin at `/opt/cs460/twin`).
- **`.github/workflows/devcontainer-image.yml`** — downloads the v2 twin into the build
  context, then builds and pushes the image to
  `ghcr.io/matthewluallen/cs460-week10-devcontainer`. This doubles as the **prebuild**: it
  re-publishes on every push to `main` that touches the devcontainer/firmware, on a weekly
  schedule, and on manual dispatch. Feature-branch pushes publish a `:<branch>` tag for
  validation without touching `:latest`.

## Rollout (two steps, non-breaking)
1. **This PR** adds the Dockerfile + workflow only. `devcontainer.json` is unchanged, so the
   current (base-image + `postCreate`) path keeps working. Merging publishes the first image.
2. **Follow-up PR ("the flip")** points `devcontainer.json` at the GHCR image and slims
   `postCreate.sh` to the fast, repo-dependent remainder:
   - link the baked twin: `cp -rn /opt/cs460/twin/* qemu/twin/`
   - `npm i --no-save --no-package-lock playwright@1.48.2` (browser already baked, no download)
   - the bus-decoder smoke check
   The `docker-in-docker` + `sshd` features and the `postStartCommand` / `forwardPorts` /
   `hostRequirements` stay. After the flip, both Codespaces and local **pull** the image
   (~1–2 min) instead of building (~10 min); a Codespaces prebuild on top makes it instant.

## Notes
- First iteration targets `linux/amd64` (Codespaces + Intel local). An `linux/arm64` variant
  (Apple-Silicon local Dev Containers) can be added as a second build platform later.
- `PLAYWRIGHT_VERSION` is pinned in the Dockerfile; keep the flip's `npm i playwright@<same>`
  in sync so the baked browser matches the npm package.
