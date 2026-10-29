#!/usr/bin/env python3
# Generates flows.json for the Plant HMI (Node-RED + node-red-contrib-modbus + node-red-dashboard).
# Layout is deliberately readable in the editor: three Modbus-read nodes on the left,
# decode function nodes in the middle, dashboard widgets on the right.
import json

PLC_HOST = "10.20.30.10"
TAB = "f_hmi"
BASE = "ui_base"

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
nodes.append({
    "id": BASE, "type": "ui_base", "theme": {
        "name": "theme-dark", "lightTheme": {"default": "#0094CE", "baseColor": "#0094CE",
            "baseFont": "-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif", "edited": True, "reset": False},
        "darkTheme": {"default": "#097479", "baseColor": "#00b3b3",
            "baseFont": "-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif", "edited": True, "reset": False},
        "customTheme": {"name": "Untitled Theme 1", "default": "#4B7930", "baseColor": "#3b6b35",
            "baseFont": "-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif"},
        "themeState": {}, "angularTheme": {"primary": "indigo", "accents": "blue", "warn": "red", "background": "grey"}},
    "site": {"name": "CS460 Plant HMI", "hideToolbar": "false", "allowSwipe": "false",
        "lockMenu": "false", "allowTempTheme": "true", "dateFormat": "DD/MM/YYYY", "sizes":
        {"sx": 48, "sy": 48, "gx": 6, "gy": 6, "cx": 6, "cy": 6, "px": 0, "py": 0}},
})
nodes.append({"id": "ui_tab", "type": "ui_tab", "name": "Plant HMI", "icon": "dashboard", "order": 1, "disabled": False, "hidden": False})
nodes.append({"id": "g_proc",  "type": "ui_group", "name": "Process",          "tab": "ui_tab", "order": 1, "disp": True, "width": 6, "collapse": False})
nodes.append({"id": "g_stat",  "type": "ui_group", "name": "Controller status","tab": "ui_tab", "order": 2, "disp": True, "width": 6, "collapse": False})
nodes.append({"id": "g_relay", "type": "ui_group", "name": "Relays & outputs",  "tab": "ui_tab", "order": 3, "disp": True, "width": 6, "collapse": False})
nodes.append({"id": "g_in",    "type": "ui_group", "name": "Inputs (incl. phone buttons)", "tab": "ui_tab", "order": 4, "disp": True, "width": 6, "collapse": False})
nodes.append({"id": "g_phone", "type": "ui_group", "name": "Phone sensors -> %IW", "tab": "ui_tab", "order": 5, "disp": True, "width": 6, "collapse": False})

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

# ---- Holding registers 0..52 -> process + status + phone ----
nodes.append(mbread("rd_hr", "Read HR 0..52", "HoldingRegister", 0, 53, 120, [["fn_proc", "fn_stat", "fn_phone"], []]))

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
    "const mode = ['BOOT','IDLE','RUN','FAULT'][r[20]] || r[20];\n"
    "return [\n"
    "  {payload: mode},\n"
    "  {payload: r[25]},                 // heartbeat\n"
    "  {payload: r[22] + ' us'},         // scan max\n"
    "  {payload: r[23]},                 // fault count\n"
    "  {payload: r[24]}                  // forces active\n"
    "];"), 5, 260, [["ui_mode"], ["ui_hb"], ["ui_scan"], ["ui_flt"], ["ui_frc"]]))
nodes.append(text("ui_mode", "g_stat", 1, "Mode", 240))
nodes.append(text("ui_hb",   "g_stat", 2, "Heartbeat", 300))
nodes.append(text("ui_scan", "g_stat", 3, "Scan max", 360))
nodes.append(text("ui_flt",  "g_stat", 4, "Faults", 420))
nodes.append(text("ui_frc",  "g_stat", 5, "Forces active", 480))

nodes.append(func("fn_phone", "phone -> IW", (
    "const r = msg.payload;\n"
    "// plc_core maps phone sensors to Modbus regs 50/51/52 when fresh\n"
    "const lux  = {payload: r[50]||0};          // ambient light (lux)\n"
    "const acc  = {payload: (r[51]||0)/100};    // |a| m/s^2 (stored x100)\n"
    "const tilt = {payload: r[52]||0};          // tilt beta (deg, signed)\n"
    "return [lux, acc, tilt];"), 3, 560, [["ui_lux"], ["ui_acc"], ["ui_tilt"]]))
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

# ---- Coils 100..107 -> digital inputs incl. phone buttons (%IX6/%IX7) ----
nodes.append(mbread("rd_di", "Read DI 100..107", "Coil", 100, 8, 860, [["fn_di"], []]))
nodes.append(func("fn_di", "DI mirror -> leds", (
    "const c = msg.payload;  // DI_MIRROR_BASE=100; index 0..7 = DI0..DI7\n"
    "msg.payload = {\n"
    "  START:!!c[0], STOP:!!c[1], ESTOP_IN:!!c[2],\n"
    "  BTN1:!!c[6], BTN2:!!c[7]   // phone momentary buttons -> %IX6/%IX7\n"
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
    "  <div class=\"di\" ng-class=\"{on:msg.payload.START}\"><div class=\"dot\"></div>DI0 START</div>\n"
    "  <div class=\"di\" ng-class=\"{on:msg.payload.STOP}\"><div class=\"dot\"></div>DI1 STOP</div>\n"
    "  <div class=\"di\" ng-class=\"{on:msg.payload.ESTOP_IN}\"><div class=\"dot\"></div>DI2 ESTOP</div>\n"
    "  <div class=\"di phone\" ng-class=\"{on:msg.payload.BTN1}\"><div class=\"dot\"></div>%IX6 BTN1</div>\n"
    "  <div class=\"di phone\" ng-class=\"{on:msg.payload.BTN2}\"><div class=\"dot\"></div>%IX7 BTN2</div>\n"
    "</div>")
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
