# Remote StampLC console (Netlify)

A tiny team-gated front end for Part 5. Students enter a team code, apply a program,
toggle the input, and watch the output. The browser never touches a device directly —
it calls a Netlify Function that checks the code and (when configured) forwards to that
team's StampLC through the gateway.

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
netlify dev    # serves the page + functions at http://localhost:8888
```
