# Setup — get the lab running (you only need a browser)

**Do this once, before Part 1. When the checklist at the bottom is green, go back to
[`README.md`](../README.md) and start the lab.** This page is only about getting the
environment up; it is not part of the lab itself.

You do **not** need to install Docker, Python, Node, a compiler, or any lab tools. The
environment builds itself in the cloud. A GitHub account and a web browser are enough.

---

## The one thing to understand first

Everything in **Parts 1–9 (the core lab)** runs inside **one cloud machine** (a GitHub
*Codespace*) using a software **twin** of the controller. You start it with a single
command — **no Docker, no containers to manage**:

```bash
./qemu/studio.sh
```

The only parts that use Docker are the optional **extensions that build the "plant
network"** (the CALDERA red-team extension and the multi-station network capture). You can
finish the whole core lab and never touch Docker. If a part doesn't say "stations" or
"CALDERA," it doesn't need Docker.

---

## Recommended path: GitHub Codespaces (nothing to install)

This runs the lab on a cloud machine and shows it in your browser (or in VS Code). It needs
**only a GitHub account** — no Docker Desktop, no local setup. It works the same on Windows,
Mac (including Apple-Silicon), and Chromebooks.

1. **Open the repo on GitHub** in your browser.
2. Click the green **`Code`** button → **`Codespaces`** tab → **`Create codespace on main`**.
3. A VS-Code-in-the-browser window opens and starts a **one-time setup** (it installs the
   SBOM tools and downloads the firmware twin). **This takes a few minutes.** You'll see
   it working in the terminal at the bottom. Wait for it to print the
   `CS460 Weeks 10–12 lab ready.` banner.
   - Setup still running = some commands will say "not found." That's expected until the
     banner appears. Don't start Part 1 until it's done.
4. **Start the core lab environment** in the terminal:
   ```bash
   ./qemu/studio.sh
   ```
5. When a **"port 8088" pop-up** appears (bottom-right), click **Open in Browser**. If you
   miss it, open the **Ports** tab, find **8088 (Logic Studio)**, and click the globe icon.
6. In that page, click **Connect twin**. You're now driving the controller. **Go to
   [`README.md`](../README.md) → Part 1.**

**Prefer desktop VS Code?** Install the **GitHub Codespaces** extension, run
*Codespaces: Create New Codespace*, pick this repo and `main`, and follow from step 3. The
Codespace still runs in the cloud — you're just viewing it from the desktop app. (This is
*not* the same as "Dev Containers / Reopen in Container," which is the advanced path below.)

---

## Optional / advanced path: run it locally (needs Docker Desktop)

Only do this if you specifically want the lab on your own machine and are comfortable
installing developer tools. **A plain VS Code install is not enough** — the local path runs
the same environment in a container on *your* computer, which requires **Docker Desktop** (a
separate, large install) plus the **Dev Containers** extension.

1. Install **Docker Desktop** and start it.
2. In VS Code, install the **Dev Containers** extension.
3. `git clone` this repo, open the folder, then run *Dev Containers: Reopen in Container*.
4. The same one-time setup runs; after the ready banner, continue exactly as in Codespaces
   step 4 (`./qemu/studio.sh`, then open `http://localhost:8088`).

Notes:
- **Apple-Silicon Mac:** this works — the setup pulls the `linux-arm64` twin automatically.
  (There is no native-macOS twin; it always runs inside the Linux container.)
- If you don't want to install Docker Desktop, use **Codespaces** above instead. Nothing in
  the lab requires the local path.

---

## If you also do the Docker extensions (E5 CALDERA / stations)

These few optional extensions bring up the multi-node "plant network" with Docker. In a
Codespace, Docker is already installed (the devcontainer ships docker-in-docker) — you don't
set anything up. You just run, from the repo root:

```bash
docker compose -f stations/docker-compose.yml up --build
```

The first build is slow (it builds CALDERA's UI). Forwarded ports: **8800** (plant HMI),
**8088** (Studio), **8888** (CALDERA UI, login `red`/`admin`). See
[`stations/README.md`](../stations/README.md) and [`caldera/README.md`](../caldera/README.md).
The core parts do **not** need this.

---

## Troubleshooting (novice-friendly)

| Symptom | What's happening / fix |
|---|---|
| `command not found` for `syft`, `pio`, etc. right after opening | The one-time setup is still running. Wait for the `lab ready.` banner. |
| Setup seemed to fail / was interrupted | Re-run it: `bash .devcontainer/postCreate.sh` |
| `./qemu/studio.sh` says the twin isn't fetched | Run `./qemu/fetch-qemu.sh` once, then try again. |
| No port pop-up | Open the **Ports** tab, click the globe icon next to **8088**. |
| Studio page loads but won't connect | Make sure `./qemu/studio.sh` is still running in the terminal, then click **Connect twin** again. |
| "Is it running?" | `./qemu/twin-status.sh` shows RUNNING/STOPPED and which build. |
| Want the text console too | Open a second terminal: `./qemu/console.sh` (runs alongside the browser Studio). |

---

## You're ready when…

- [ ] The terminal printed **`CS460 Weeks 10–12 lab ready.`**
- [ ] `./qemu/studio.sh` is running and port **8088** opened in your browser.
- [ ] Clicking **Connect twin** shows the controller responding.

✅ All three? **Go to [`README.md`](../README.md) and start Part 1.** You won't need this
page again.
