# Remote StampLC console (Netlify)

> **Optional path.** The canonical Part 1 path is each student driving **their own twin**
> (`./qemu/run.sh` + the `plc>` console or Logic Studio — see [`wiring/`](../wiring/) and the
> main [README](../README.md)). This team-gated remote console is an **optional** shared-bench
> alternative for in-class use; it is not required to complete Part 1.

A tiny team-gated front end for Part 1. Students enter a team code, apply a program,
toggle the input, and watch the output. The browser never touches a device directly —
it calls a Netlify Function that checks the code and (when configured) forwards to that
team's StampLC through the gateway.

## Portable projects in Logic Studio

Use [Logic Studio](public/logic_studio.html).
The [reboot RUN project](public/projects/reboot-run-indicator.csco) and
[matching Studio ST](public/projects/reboot-run-indicator.st) demonstrate an explicit
startup request. Reboot defaults to PROGRAM; a new project load always stays PROGRAM.

A gateway forwarding commands over USB or RS-485 crosses the runtime's trusted
serial boundary, which is outside its IP host rules. Enforce user/host permissions
at that gateway; forwarding a serial stream does not preserve the original network
client's identity for controller authorization.

## It already runs with no device (mock mode)

With no environment variables set, the Function runs in **mock mode**: it validates the
code and the page simulates the PLC locally. That's enough to demo the whole flow and to
develop against before the hardware/gateway is up.

Default mock codes: Team 1 `alpha`, Team 2 `bravo`, Team 3 `charlie`, Team 4 `delta`.

## Deploy

From the repo root:

```bash
npm i -g netlify-cli      # once
netlify deploy --build    # preview URL
netlify deploy --build --prod
```

Tear it down after class from the Netlify dashboard (or `netlify sites:delete`).

## Wire it to the real devices (later)

Set these environment variables in the Netlify UI (Site settings → Environment variables):

| Variable | Example | Purpose |
|---|---|---|
| `TEAM_CODES` | `{"1":"...","2":"...","3":"...","4":"..."}` | real per-team access codes |
| `DEVICE_ENDPOINTS` | `{"1":"https://<funnel-host>/team1", ...}` | each team's device, via the Tailscale Funnel gateway |

With `DEVICE_ENDPOINTS` set for a team, the Function forwards `apply` / `trigger` /
`status` to `<endpoint>/<action>` and returns the device's JSON. The device/gateway only
needs to expose those three POST routes; keep Modbus/telnet/OTA off the public path.

Local development:

```bash
netlify dev    # serves the page + the Netlify Function at http://localhost:8888
```

Use `netlify dev` only when you need the **Function** — team-code gating and the
`apply`/`trigger`/`status` forwarding. To just **view the browser tools** (Logic Studio,
Protocol Studio, Operator panel, Team Console) as static pages, you don't need Netlify:

```bash
./tools/serve-web.sh   # serves web/public on :8000 (same tool Parts 1/2/3 use)
```

Served statically the Team Console page loads, but its `apply`/`trigger`/`status` calls hit
the Netlify Function path, which only exists under `netlify dev` or a deploy — so use
`netlify dev` when you want the gated flow, `serve-web.sh` when you just want the pages.
