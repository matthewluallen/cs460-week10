# The capture pipeline — how we screenshot the lab reliably

This is the **reliable, reusable** way to capture the lab's live tools (Logic Studio,
HMI, analyzers) and any GUI tool (Wireshark, Ghidra) as clean PNGs for the Canvas lab +
READMEs. It was arrived at the hard way; use it instead of re-deriving it.

The golden rule: **capture from inside the Codespace, never by driving a forwarded
github.dev port with an external browser.**

## Why not the external browser
- Driving a forwarded `*.app.github.dev` port with Claude-in-Chrome **froze the renderer**
  on the Studio. The freeze was the Studio's `prompt()` in `Connect twin` (a native dialog
  that blocks the page), not a Studio bug — but it makes external-browser capture unreliable.
- Chrome's `save_to_disk` screenshots land on **whatever host runs Chrome, not the Mac**,
  so the `Control your Mac` osascript bridge can't read them. Dead end.

## A. Web tool served in the Codespace (Studio :8088, HMI :8800, QR page, …)
Headless **Playwright/Chromium inside the Codespace against `localhost`** — fast, no
freeze, no interstitial, and the PNG lands straight in `docs/img/`.

1. One-time setup in the Codespace (already done; redo if the Codespace is fresh):
   ```bash
   mkdir -p /workspaces/cs460-week10/.capture && cd $_
   npm init -y >/dev/null && npm i playwright@1.49.1
   npx playwright install chromium
   sudo apt-get install -y libdbus-1-3 libatk1.0-0t64 libatk-bridge2.0-0t64 \
     libatspi2.0-0t64 libxcomposite1 libxdamage1 libxfixes3 libxrandr2 libgbm1 \
     libdrm2 libxkbcommon0 libasound2t64 libnss3 libnspr4 libcups2 libpango-1.0-0 libcairo2
   ```
2. A capture script (`.capture/*.js`) launches chromium headless, drives `localhost:<port>`,
   and `page.screenshot({path:'/workspaces/cs460-week10/docs/img/<name>.png'})`.
   - **Connect the twin headlessly without the prompt dialog:**
     `await page.evaluate(() => connectTwin(twinDefaultUrl()))`. `send()` returning `true`
     means the WS is live.
   - **Energize points:** `await page.evaluate(() => send('force QX1 1'))` / `force IX1 1`.
   - Protocol + Circuit workspaces are live over HTTP `/modbus` with no WS needed.
   - Use `deviceScaleFactor: 2` for crisp text; auto-size height to `document.body.scrollHeight`.

## B. Any GUI tool (Wireshark, Ghidra) — Docker + noVNC + the KasmVNC screenshot API
The noVNC **web client renders blank under headless Playwright** (needs a gesture). The
reliable grab is the **KasmVNC framebuffer API**:
```bash
docker run -d --name ws --cap-add NET_ADMIN -p 3010:3000 \
  -v /tmp/wscfg:/config -e PUID=1000 -e PGID=1000 lscr.io/linuxserver/wireshark:latest
# drive the GUI once (open a file, apply a filter) in a browser on the forwarded port, then:
curl -s http://localhost:3010/api/screenshot -o shot.png      # full 3024x1544 framebuffer
```
- `/api/get_screenshot?width=…` 404s on this image; plain **`/api/screenshot`** works.
- Put files the GUI must open under the container's `/config` (mount a host dir); its file
  chooser opens there and the `abc` (PUID 1000) user can read them — `chmod 644` host-side.
- **Memory:** the Wireshark image is 2.6 GB; running it **and** the stations twin OOM-kills
  the 8 GB Codespace. Run GUI-tool captures and the stations stack **sequentially**.

## C. CLI output → a clean "terminal" PNG
For tool output (binwalk, strings, esptool, objdump), render the **real** output in a small
dark terminal-styled HTML (`.capture/*.html`) and screenshot it with the A-pipeline. The
text is genuine tool output; only the chrome is styled. Examples produced this way:
`part2-firmware-forensics.png`, `part6-firmware-re-disasm.png`.

## D. Moving files around (Mac bridge ↔ Codespace ↔ container)
- **Write a script into the Codespace:** base64 it in the container, then
  `gh codespace ssh -c <CS> -- 'echo <B64> | base64 -d > /workspaces/.../x.js'`
  (base64's alphabet is shell-safe inside both the osascript double-quotes and the ssh
  single-quotes).
- **Run long jobs detached** (osascript `do shell script` ~120 s cap):
  `setsid bash -c "…; echo DONE" >/tmp/log 2>&1 </dev/null & echo started`, then poll
  `/tmp/log`. Avoid `nohup … &` — it trips `gh codespace ssh` to exit 255.
- **Pull PNGs to the Mac:** `gh codespace cp -e -c <CS> 'remote:/workspaces/.../docs/img/x.png' .`
  (the `-e` glob form; the single-file form without `-e` has failed).
- **View a PNG here:** copy to a connected folder (`/Users/matthewluallen/cs460-week10-stamplc/.capshots/`),
  `device_stage_files` it, then `Read`.
- **Commit:** copy the staged PNG into the container repo `docs/img/` and commit/push from
  the container (its git push works; the Codespace ssh session has no push token).

## Quoting gotchas (osascript → ssh)
- Parentheses in an `echo` (`# step (x)`) break the remote `sh -c`. Avoid them.
- Go-template `{{…}}` in `docker inspect -f` nested inside the quotes is fragile — prefer
  `docker network inspect … | grep`, or write a tiny script file and run it.

## Environment pointers
- Codespace: `mysterious-phantom-pvqwwxpxg99h9vj` on `matthewluallen/cs460-week10` main.
- Studio+twin: `./qemu/studio.sh` (serves :8088, binds the twin's Modbus :502 via W5500).
- Build ELF (for objdump RE): `sbom/firmware/.pio/build/m5stamplc/firmware.elf` on the Mac;
  disassembler `~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump`.
