#!/usr/bin/env python3
# Generates flows.json for the Plant HMI (Node-RED + node-red-contrib-modbus + node-red-dashboard).
# Layout is deliberately readable in the editor: three Modbus-read nodes on the left,
# decode function nodes in the middle, dashboard widgets on the right.
import json

PLC_HOST = "10.20.30.10"
TAB = "f_hmi"
BASE = "uib0"

nodes = []

# ---- config: Modbus TCP client ----
nodes.append({
    "id": "mb_plc", "type": "modbus-client", "name": f"PLC {PLC_HOST}:502",
    "clienttype": "tcp", "bufferCommands": True, "stateLogEnabled": False,
    "queueLogEnabled": False, "failureLogEnabled": True,
    "tcpHost": PLC_HOST, "tcpPort": "502", "tcpType": "DEFAULT",
    "serialPort": "", "serialType": "RTU-BUFFERD", "serialConnectionDelay": "100",
    "serialBaudrate": "9600", "serialDatabits": "8", "serialStopbits": "1", "serialParity": "none",
    "unit_id": "1", "commandDelay": "1", "clientTimeout": "1000",
    "reconnectOnTimeout": True, "reconnectTimeout": "2000", "parallelUnitIdsAllowed": True,
    "showErrors": False, "showWarnings": True, "showLogs": False,
})

# ---- dashboard config ----
# node-red-dashboard (legacy angular, 3.6.5) requires exactly one ui_base and runs cleanly on
# node-red 3.1.x (the Dockerfile pins that; node-red 4.x's config-node resolver mis-flags the
# dashboard's ui_base<->ui_tab link as a circular dependency).
nodes.append({
    "id": BASE, "type": "ui_base", "theme": {
        "name": "theme-dark",
        "lightTheme": {"default": "#0094CE", "baseColor": "#0094CE",
            "baseFont": "-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif", "edited": True, "reset": False},
        "darkTheme": {"default": "#097479", "baseColor": "#00b3b3",
            "baseFont": "-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif", "edited": True, "reset": False},
        "customTheme": {"name": "Untitled Theme 1", "default": "#4B7930", "baseColor": "#3b6b35",
            "baseFont": "-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif"},
        "themeState": {"base-color": {"default": "#097479", "value": "#097479", "edited": False},
            "page-titlebar-backgroundColor": {"value": "#097479", "edited": False},
            "page-backgroundColor": {"value": "#111827", "edited": False},
            "page-sidebar-backgroundColor": {"value": "#1b2330", "edited": False},
            "group-textColor": {"value": "#d9e2ec", "edited": False},
            "group-borderColor": {"value": "#2b3648", "edited": False},
            "group-backgroundColor": {"value": "#1b2330", "edited": False},
            "widget-textColor": {"value": "#eaf6ff", "edited": False},
            "widget-backgroundColor": {"value": "#097479", "edited": False},
            "widget-borderColor": {"value": "#1b2330", "edited": False},
            "base-font": {"value": "-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif"}},
        "angularTheme": {"primary": "indigo", "accents": "blue", "warn": "red", "background": "grey"}},
    "site": {"name": "CS460 Plant HMI", "hideToolbar": "false", "allowSwipe": "false",
        "lockMenu": "false", "allowTempTheme": "true", "dateFormat": "DD/MM/YYYY",
        "sizes": {"sx": 48, "sy": 48, "gx": 6, "gy": 6, "cx": 6, "cy": 6, "px": 0, "py": 0}},
})
nodes.append({"id": "uitab0", "type": "ui_tab", "name": "Plant HMI", "icon": "dashboard", "order": 1, "disabled": False, "hidden": False})
nodes.append({"id": "g_proc",  "type": "ui_group", "name": "Process",          "tab": "uitab0", "order": 1, "disp": True, "width": 6, "collapse": False})
nodes.append({"id": "g_stat",  "type": "ui_group", "name": "Controller status","tab": "uitab0", "order": 2, "disp": True, "width": 6, "collapse": False})
nodes.append({"id": "g_relay", "type": "ui_group", "name": "Relays & outputs",  "tab": "uitab0", "order": 3, "disp": True, "width": 6, "collapse": False})
nodes.append({"id": "g_in",    "type": "ui_group", "name": "Wired inputs IN1-IN8", "tab": "uitab0", "order": 4, "disp": True, "width": 6, "collapse": False})
nodes.append({"id": "g_phone", "type": "ui_group", "name": "Phone points (network-only)", "tab": "uitab0", "order": 5, "disp": True, "width": 6, "collapse": False})

def mbread(nid, name, dtype, adr, qty, y, wires):
    return {"id": nid, "type": "modbus-read", "name": name, "topic": "", "showStatusActivities": False,
            "showErrors": False, "showWarnings": True, "logIOActivities": False, "unitid": "1",
            "dataType": dtype, "adr": str(adr), "quantity": str(qty), "rate": "1", "rateUnit": "s",
            "delayOnStart": False, "startDelayTime": "", "server": "mb_plc", "useIOFile": False, "ioFile": "",
            "useIOForPayload": False, "emptyMsgOnFail": True, "keepMsgProperties": False,
            "x": 150, "y": y, "wires": wires}

def func(nid, name, code, outputs, y, wires, x=380):
    return {"id": nid, "type": "function", "name": name, "func": code, "outputs": outputs,
            "timeout": 0, "noerr": 0, "initialize": "", "finalize": "", "libs": [], "x": x, "y": y, "wires": wires}

def gauge(nid, group, order, title, label, vmin, vmax, y, colors=None):
    return {"id": nid, "type": "ui_gauge", "name": title, "group": group, "order": order, "width": 0, "height": 0,
            "gtype": "gage", "title": title, "label": label, "format": "{{value}}", "min": str(vmin), "max": str(vmax),
            "colors": colors or ["#00b500", "#e6e600", "#ca3838"], "seg1": "", "seg2": "", "diff": False,
            "className": "", "x": 650, "y": y, "wires": []}

def text(nid, group, order, label, y):
    return {"id": nid, "type": "ui_text", "group": group, "order": order, "width": 0, "height": 0, "name": label,
            "label": label, "format": "{{msg.payload}}", "layout": "row-spread", "className": "", "style": False,
            "font": "", "fontSize": 16, "color": "#ffffff", "x": 650, "y": y, "wires": []}

# ---- Holding registers 0..32 -> process + status; 140..165 -> network-only phone points ----
nodes.append(mbread("rd_hr", "Read HR 0..32", "HoldingRegister", 0, 33, 120, [["fn_proc", "fn_stat"], []]))
nodes.append(mbread("rd_phone", "Read HR 140..165 (phone)", "HoldingRegister", 140, 26, 560, [["fn_phone"], []]))

nodes.append(func("fn_proc", "process", (
    "const r = msg.payload;\n"
    "// reg2 = temp x10, reg30 = setpoint x10 (see plc_point_map.h)\n"
    "const temp = {payload: (r[2]||0)/10};\n"
    "const sp   = {payload: (r[30]||0)/10};\n"
    "return [temp, sp];"), 2, 120, [["ui_temp"], ["ui_sp"]]))
nodes.append(gauge("ui_temp", "g_proc", 1, "Temperature", "°C", 0, 80, 80))
nodes.append(gauge("ui_sp",   "g_proc", 2, "Setpoint",    "°C", 0, 80, 160))

nodes.append(func("fn_stat", "status", (
    "const r = msg.payload;\n"
    "const n = v => (v==null ? 0 : v);   // Modbus may return a short array before RUN\n"
    "const mode = ['BOOT','IDLE','RUN','FAULT'][n(r[20])] || ('mode '+n(r[20]));\n"
    "return [\n"
    "  {payload: mode},\n"
    "  {payload: n(r[25])},              // heartbeat\n"
    "  {payload: n(r[22]) + ' us'},      // scan max\n"
    "  {payload: n(r[23])},              // fault count\n"
    "  {payload: n(r[24])}               // forces active\n"
    "];"), 5, 260, [["ui_mode"], ["ui_hb"], ["ui_scan"], ["ui_flt"], ["ui_frc"]]))
nodes.append(text("ui_mode", "g_stat", 1, "Mode", 240))
nodes.append(text("ui_hb",   "g_stat", 2, "Heartbeat", 300))
nodes.append(text("ui_scan", "g_stat", 3, "Scan max", 360))
nodes.append(text("ui_flt",  "g_stat", 4, "Faults", 420))
nodes.append(text("ui_frc",  "g_stat", 5, "Forces active", 480))

nodes.append(func("fn_phone", "phone points", (
    "const r = msg.payload;  // index 0 = register 140 (sbom/firmware/point_map.json phone_points)\n"
    "// Every phone point has its own 8 s freshness bit (reg 159 = points 0-15, reg 160 = 16+).\n"
    "// A stale value FREEZES at its last sample, so show 0 unless the point is fresh.\n"
    "const n = v => (v==null ? 0 : v);\n"
    "const s16 = v => (n(v) & 0x8000) ? n(v) - 0x10000 : n(v);\n"
    "const fresh = (n(r[20]) << 16 | n(r[19])) >>> 0;\n"
    "const f = i => ((fresh >>> i) & 1) === 1;\n"
    "const lux  = {payload: f(0) ? n(r[0]) : 0};          // phone_light_lux (reg 140)\n"
    "const acc  = {payload: f(1) ? n(r[1]) / 100 : 0};    // phone_accel_x100 (reg 141)\n"
    "const tilt = {payload: f(6) ? s16(r[6]) : 0};        // phone_tilt_b (reg 146)\n"
    "const btn  = {payload: {BTN1: f(15) && n(r[17]) === 1, BTN2: f(16) && n(r[18]) === 1}}; // regs 157/158\n"
    "return [lux, acc, tilt, btn];"), 4, 560, [["ui_lux"], ["ui_acc"], ["ui_tilt"], ["ui_pbtn"]]))
nodes.append(gauge("ui_lux",  "g_phone", 1, "Light", "lux",   0, 1000, 540))
nodes.append(gauge("ui_acc",  "g_phone", 2, "Accel", "m/s²", 0, 30, 620))
nodes.append(gauge("ui_tilt", "g_phone", 3, "Tilt β", "°", -90, 90, 700,
                   colors=["#ca3838", "#00b500", "#ca3838"]))

# ---- Coils 0..9 -> relay/output LEDs (one ui_template renders the LED row) ----
nodes.append(mbread("rd_coil", "Read coils 0..9", "Coil", 0, 10, 760, [["fn_relay"], []]))
nodes.append(func("fn_relay", "coils -> leds", (
    "const c = msg.payload;  // array of booleans, index = coil #\n"
    "// map: 0 ESTOP,1 RELAY0,2 RELAY1,3 RELAY2,4 RELAY3,5 SSR1,6 SSR2,9 CMD_RUN\n"
    "msg.payload = {\n"
    "  ESTOP:!!c[0], QX0:!!c[1], QX1:!!c[2], QX2:!!c[3], QX3:!!c[4],\n"
    "  SSR1:!!c[5], SSR2:!!c[6], RUN:!!c[9]\n"
    "};\nreturn msg;"), 1, 760, [["ui_relays"]]))

led_tpl = (
    "<style>\n"
    ".ledrow{display:flex;flex-wrap:wrap;gap:10px}\n"
    ".led{flex:1 1 60px;text-align:center;padding:8px 4px;border-radius:8px;background:#1b2330;color:#9fb0c3;font:600 12px system-ui}\n"
    ".led .dot{width:14px;height:14px;border-radius:50%;margin:0 auto 6px;background:#30405a;box-shadow:inset 0 0 3px #000}\n"
    ".led.on{color:#eaf6ff}\n"
    ".led.on .dot{background:#39d98a;box-shadow:0 0 10px #39d98a}\n"
    ".led.estop.on .dot{background:#ff5d5d;box-shadow:0 0 10px #ff5d5d}\n"
    "</style>\n"
    "<div class=\"ledrow\">\n"
    "  <div class=\"led estop\" ng-class=\"{on:msg.payload.ESTOP}\"><div class=\"dot\"></div>E-STOP</div>\n"
    "  <div class=\"led\" ng-class=\"{on:msg.payload.QX0}\"><div class=\"dot\"></div>QX0 R0</div>\n"
    "  <div class=\"led\" ng-class=\"{on:msg.payload.QX1}\"><div class=\"dot\"></div>QX1 R1</div>\n"
    "  <div class=\"led\" ng-class=\"{on:msg.payload.QX2}\"><div class=\"dot\"></div>QX2 R2</div>\n"
    "  <div class=\"led\" ng-class=\"{on:msg.payload.QX3}\"><div class=\"dot\"></div>QX3 R3</div>\n"
    "  <div class=\"led\" ng-class=\"{on:msg.payload.SSR1}\"><div class=\"dot\"></div>SSR1</div>\n"
    "  <div class=\"led\" ng-class=\"{on:msg.payload.SSR2}\"><div class=\"dot\"></div>SSR2</div>\n"
    "  <div class=\"led\" ng-class=\"{on:msg.payload.RUN}\"><div class=\"dot\"></div>RUN</div>\n"
    "</div>")
nodes.append({"id": "ui_relays", "type": "ui_template", "group": "g_relay", "name": "Relay LEDs",
              "order": 1, "width": 6, "height": 2, "format": led_tpl, "storeOutMessages": True,
              "fwdInMessages": True, "resendOnRefresh": True, "templateScope": "local",
              "className": "", "x": 650, "y": 760, "wires": [[]]})

# ---- Coils 100..107 -> the wired inputs IN1..IN8 (DI0..DI7). Phone buttons are NOT here:
# they are network-only phone points (regs 157/158), shown in the phone group. ----
nodes.append(mbread("rd_di", "Read DI 100..107", "Coil", 100, 8, 860, [["fn_di"], []]))
nodes.append(func("fn_di", "DI mirror -> leds", (
    "const c = msg.payload;  // DI_MIRROR_BASE=100; index 0..7 = DI0..DI7 = IN1..IN8\n"
    "// names: the factory motor program's tags (motor-start-stop-estop)\n"
    "msg.payload = {\n"
    "  START:!!c[0], STOP:!!c[1], ESTOP_OK:!!c[2], RESET:!!c[3]\n"
    "};\nreturn msg;"), 1, 860, [["ui_di"]]))
di_tpl = (
    "<style>\n"
    ".drow{display:flex;flex-wrap:wrap;gap:10px}\n"
    ".di{flex:1 1 70px;text-align:center;padding:8px 4px;border-radius:8px;background:#1b2330;color:#9fb0c3;font:600 12px system-ui}\n"
    ".di .dot{width:14px;height:14px;border-radius:50%;margin:0 auto 6px;background:#30405a}\n"
    ".di.on{color:#eaf6ff}.di.on .dot{background:#4aa3ff;box-shadow:0 0 10px #4aa3ff}\n"
    ".di.phone.on .dot{background:#b084ff;box-shadow:0 0 10px #b084ff}\n"
    "</style>\n"
    "<div class=\"drow\">\n"
    "  <div class=\"di\" ng-class=\"{on:msg.payload.START}\"><div class=\"dot\"></div>IN1 start_pb</div>\n"
    "  <div class=\"di\" ng-class=\"{on:msg.payload.STOP}\"><div class=\"dot\"></div>IN2 stop_pb</div>\n"
    "  <div class=\"di\" ng-class=\"{on:msg.payload.ESTOP_OK}\"><div class=\"dot\"></div>IN3 estop_ok (NC: lit = healthy)</div>\n"
    "  <div class=\"di\" ng-class=\"{on:msg.payload.RESET}\"><div class=\"dot\"></div>IN4 reset_pb</div>\n"
    "</div>")
pbtn_tpl = (
    "<style>.drow{display:flex;gap:10px}.di{flex:1 1 70px;text-align:center;padding:8px 4px;border-radius:8px;"
    "background:#1b2330;color:#9fb0c3;font:600 12px system-ui}.di .dot{width:14px;height:14px;border-radius:50%;"
    "margin:0 auto 6px;background:#30405a}.di.on{color:#eaf6ff}.di.on .dot{background:#b084ff;box-shadow:0 0 10px #b084ff}</style>\n"
    "<div class=\"drow\">\n"
    "  <div class=\"di\" ng-class=\"{on:msg.payload.BTN1}\"><div class=\"dot\"></div>phone_btn1 (reg 157)</div>\n"
    "  <div class=\"di\" ng-class=\"{on:msg.payload.BTN2}\"><div class=\"dot\"></div>phone_btn2 (reg 158)</div>\n"
    "</div>")
nodes.append({"id": "ui_pbtn", "type": "ui_template", "group": "g_phone", "name": "Phone buttons",
              "order": 4, "width": 6, "height": 2, "format": pbtn_tpl, "storeOutMessages": True,
              "fwdInMessages": True, "resendOnRefresh": True, "templateScope": "local",
              "className": "", "x": 650, "y": 780, "wires": [[]]})
nodes.append({"id": "ui_di", "type": "ui_template", "group": "g_in", "name": "Input LEDs",
              "order": 1, "width": 6, "height": 2, "format": di_tpl, "storeOutMessages": True,
              "fwdInMessages": True, "resendOnRefresh": True, "templateScope": "local",
              "className": "", "x": 650, "y": 860, "wires": [[]]})

# attach all flow nodes to the tab
for n in nodes:
    if n["type"] in ("modbus-read", "function", "ui_gauge", "ui_text", "ui_template"):
        n["z"] = TAB

flows = [{"id": TAB, "type": "tab", "label": "Plant HMI", "disabled": False, "info":
          "HMI station: Modbus/TCP poll of the StampLC twin -> decode -> dashboard. "
          "Left: three Modbus-read nodes. Middle: decode functions. Right: dashboard widgets (/ui).",
          "env": []}] + nodes

with open("flows.json", "w") as f:
    json.dump(flows, f, indent=4)
print("wrote flows.json with", len(flows), "nodes")
