// CS460 Week 10 — team-gated device proxy.
//
// The browser never talks to a StampLC directly. It POSTs here; this function
// checks the team code, then (if a device is configured) forwards the action to
// that team's endpoint through the gateway. Until a device is wired, it runs in
// MOCK mode and the page simulates the PLC locally.
//
// Configure in the Netlify UI (Site settings -> Environment variables):
//   TEAM_CODES        {"1":"...","2":"...","3":"...","4":"..."}   (per-team access codes)
//   DEVICE_ENDPOINTS  {"1":"https://<funnel-host>/team1", ...}    (optional; omit = mock)
//
// Actions: "apply" {program}, "trigger" {on} (gateway maps to `force di0 <on>`),
//          "status" {}, "console" {command}.

const DEFAULT_CODES = { "1": "alpha", "2": "bravo", "3": "charlie", "4": "delta" };

exports.handler = async (event) => {
  if (event.httpMethod !== "POST") return json(405, { error: "POST only" });

  let body;
  try { body = JSON.parse(event.body || "{}"); }
  catch { return json(400, { error: "malformed request" }); }

  const { team, code, action, payload } = body;
  const codes = parseEnv("TEAM_CODES", DEFAULT_CODES);

  if (!team || !codes[String(team)] || codes[String(team)] !== code) {
    return json(401, { error: "wrong team or code" });
  }
  if (!["apply", "trigger", "status", "console"].includes(action)) {
    return json(400, { error: "unknown action" });
  }

  const base = parseEnv("DEVICE_ENDPOINTS", {})[String(team)];

  // No device configured yet -> mock: acknowledge and let the page simulate.
  if (!base) {
    if (action === "console") {
      return json(200, { ok: true, mock: true, team: String(team), action, output: mockConsole((payload && payload.command) || "") });
    }
    return json(200, { ok: true, mock: true, team: String(team), action, echo: payload || null });
  }

  // Real device: forward through the gateway.
  try {
    const r = await fetch(base.replace(/\/+$/, "") + "/" + action, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(payload || {}),
    });
    const text = await r.text();
    return json(r.status, { ok: r.ok, mock: false, team: String(team), action, device: safeJson(text) });
  } catch (e) {
    return json(502, { error: "device unreachable", detail: String(e && e.message || e) });
  }
};

function parseEnv(name, dflt) { try { return JSON.parse(process.env[name] || ""); } catch { return dflt; } }
function safeJson(t) { try { return JSON.parse(t); } catch { return t; } }
function json(statusCode, obj) {
  return { statusCode, headers: { "content-type": "application/json" }, body: JSON.stringify(obj) };
}

// Simulated plc> console for MOCK mode (no device wired yet). The real device's
// output comes back verbatim once DEVICE_ENDPOINTS is set and the gateway bridges
// the command to the StampLC's telnet console.
function mockConsole(cmd) {
  const c = String(cmd).trim().toLowerCase();
  if (!c) return "plc> ";
  if (c === "help" || c === "?")
    return "commands:\n  status      CPU mode, scan time, faults\n  io          digital inputs and relay commanded/actual\n  i2cscan     probe the I2C bus\n  diag        health snapshot (chip, heap, PLC)\n  points      list named points and live values\n  (MOCK console — real device returns live output)";
  if (c === "status")
    return "mode=PROGRAM  scan=1.9ms  faults=0  forces=0  comms: Modbus/TCP :502 up  (MOCK)";
  if (c === "io")
    return "DI : 0 0 0 0 0 0 0 0\nRELAY cmd/act : 0/0 0/0 0/0 0/0   (MOCK)";
  if (c === "i2cscan")
    return "I2C scan: 0x32 RTC  0x40 INA226  0x43 PI4IOE(buttons)  0x48 LM75  0x59 AW9523(relays+DI)  (MOCK — real device lists what's on the bus)";
  if (c === "diag")
    return "chip=ESP32-S3  heap=210KB free  plc=running(5 instr)  net=offline  (MOCK)";
  return "plc> " + c + ": (MOCK console — this command runs live on the real device)";
}
