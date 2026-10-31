# Read-only initial bench capture

This utility records a bounded set of **existing normal USB console replies** from
one explicitly chosen controller. It does not flash firmware, load a project,
change mode, arm a source, change sampling, scan a bus, write an output, add or clear
a force, clear faults, reset timing, or request a reboot. It does not establish
that any particular module is connected or that an output physically changed.

The same tool observes either build. It records the reported `firmware_profile`
and ELF hash when available; it never changes `ENABLE_CWE_DEMOS`, selects a profile,
or infers the secure build from a banner. Preserve the vulnerable **and** secure
builds for the Part-9 comparison.

`capture.py` is the normal-console observation tool described below; it only reads.

## Before opening the port

Leave the controller in its current normal operating state. This first capture
does not require a special programming mode or a mode change. Close competing
serial terminals/Studio connections and select the correct physical USB console
port yourself. The utility never guesses from the first port or a USB vendor ID.

**Opening or closing a serial port can reset some boards.** This script never calls
DTR/RTS setters, BREAK, a buffer reset or a reset command. However, pySerial applies
its line state on open, and the OS/driver can change those lines automatically.
There is no portable promise that opening the port leaves the hardware untouched.
Do not use this procedure on an energized process that cannot tolerate that event.
A saved opt-in startup RUN request can act after a reset. The
[pySerial open documentation](https://pyserial.readthedocs.io/en/latest/pyserial_api.html#serial.Serial.open)
describes the library/driver limitation. No esptool-style reset sequence is used.

Read-only commands also consume console/CPU time. The existing firmware updates
console counters and temporarily suppresses normal telemetry retention when input
arrives. This capture is an observation with those effects, not a timing-neutral
measurement or a backup of every controller setting.

## Run on the computer physically connected to the board

Use Python 3.9 or later. Only the explicit `capture` and `list-ports` actions need
pySerial; the plan, help and offline tests use the standard library.

```bash
python3 -m pip install pyserial==3.5
python3 tools/bench/capture.py plan
python3 tools/bench/capture.py list-ports
```

On Windows, replace `python3` with `py -3`. The
[port enumeration API](https://pyserial.readthedocs.io/en/latest/tools.html#serial.tools.list_ports.comports)
lists OS metadata without opening ports. A description/VID/PID is a selection aid,
not authenticated controller identity or module detection. The script does not use
remote serial URLs, network gateways or account credentials.

Choose one local port from that list and a **new** output directory:

```bash
# macOS: substitute the actual port printed by list-ports.
python3 tools/bench/capture.py capture --port /dev/cu.usbmodem1234 --output bench-first

# Linux example; do not assume this is your assigned device.
python3 tools/bench/capture.py capture --port /dev/ttyACM0 --output bench-first

# Windows example, from the repository root.
py -3 tools/bench/capture.py capture --port COM9 --output bench-first
```

Only `capture` opens the selected port and sends the fixed plan. Importing the
module, help, `plan`, and `list-ports` never send commands. There is no arbitrary
command option and no automatic retry, reconnect or reset.

### Normal-console readiness

The tool first listens without sending anything. It needs an empty, line-boundary
`plc> ` prompt. It stops on the startup configuration menu, bootloader or serial
gateway markers, partial input, or an unknown endpoint state. It does not send
Enter to wake the board: doing so could enter a configuration menu or execute a
previously typed partial command.

An already running console may have printed its prompt before this process opened
the port. If **you just observed this board's empty normal `plc> ` prompt**, closed
that terminal, and know no partial command remains, you may explicitly assert that
fact:

```bash
python3 tools/bench/capture.py capture --port /dev/cu.usbmodem1234 --output bench-first --console-ready
```

The manifest records `user_asserted_empty_normal_console`, rather than claiming
that the tool observed the prompt. This option applies only when passive listening
receives no bytes; it cannot override a menu, bootloader, partial line or other
unexpected output. A port listing or a normal-looking LCD is not this assertion.
If readiness remains unknown, retain that result and coordinate access through the
existing console. Do not reboot merely to satisfy this capture tool.

## Exact capture plan

| Request | Evidence retained | Boundary |
|---|---|---|
| `status`, before and after | Mode, scan last/min/max, counters, faults/force counts, cached IO-exp presence and uptime | Controller summary; not physical relay/contact feedback |
| `project json`, before and after | Reported profile, ELF/payload hashes, boot/project generation, installed state, startup, host policy and expected output-module identity when supported | Not a full portable `.csco`; does not return every mapping/default/symbol |
| `ver` | Firmware's own version banner | No profile or authenticity inference from this string |
| `program` | Bounded VM instruction dump with END marker | No reconstruction of complete authoring source; another runtime profile may not support this form |
| `devices status` | Existing inventory, declared discovery profile, route/family/revision information and scan age | Reads the cached inventory; never runs `devices scan` |
| `devices samples` | Existing acquisition quality, values, age, epochs and counters | Does not enable acquisition or commission a module |
| `bindings json` | Existing process-input configuration and sample evidence | Does not load/apply/arm a binding |
| `portc status` | Active/saved Port C ownership and counters | No UART read, send, baud/profile change or physical identity claim |
| `proto json` | Configured service flags and implementation labels | No service change; a flag is not proof a network endpoint is working |
| `execution json` | Optional `tbd.execution-readback/v1` timing/watchdog/first-failure report | Older firmware can explicitly reject it; returned timing remains controller-reported evidence |

`diag`, `diag json`, `support`, `svcinfo`, `netcfg`, logs and RAM dumps are excluded.
In the inspected firmware, diagnostics can include a provisioning seed, support
also probes I2C, and service information can expose credentials. No bus probing,
self-test, authentication, factory reset or firmware update occurs in this plan.

The available USB `status` timing is retained even when `execution json` is absent.
The separate HTTP `/api/timing` route is not contacted or guessed from an IP address.
No module names, current configuration or presence flags are filled in from the
expected bench inventory. Missing evidence stays missing.

## Files, bounds and interpretation

`manifest.json` records the plan, chosen port, host timestamps, bounded receive
counts, per-command timing, framing/result status, available parsed replies and
start/end identity comparison. Matching identity means two controller reports
agreed; it is not attestation. A changed boot/hash/generation makes the result
`identity_changed`, and those observations must not be merged as one installation.

Each `*.reply.raw` contains the recognized reply lines **byte for byte**, including
their original line endings. Echoes and prompts are omitted; offsets, file hashes
and omitted-byte counts are in the manifest. Initial/unrelated unsolicited output
is not saved as a serial transcript. A mixed response is marked `mixed_reply` and
stops further commands. A recognized credential-field marker withholds that entire
reply and records `withheld_sensitive_output`. This is a conservative exclusion
for these known commands, not a general-purpose secret detector; review project
names, tags and network addresses before sharing the files. Nothing is uploaded.

Default passive observation is 2 seconds and each reply has 3 seconds. Both may be
set from 0.5 to 10 with `--observe-seconds` and `--reply-seconds`; the overall read
budget remains 45 seconds. Each window is capped at 32 KiB, all received bytes at
256 KiB, each read at 512 bytes, and the serial read/write timeouts at 50/500 ms.
These bound capture operations, not OS driver time during port open/close.

A complete reply requires the expected form **and** a settled console prompt.
Truncation, no prompt, timeout, partial write, unexpected data or transport failure
stops subsequent requests; partial evidence remains available. An explicit older
firmware rejection records `unsupported` and permits the next read-only request
after its prompt. Result `complete_with_gaps` is not a failure of the module or a
hardware pass. Even result `complete` means the requested reports were captured;
it does not mean the project, timing, watchdog, modules or physical process passed.

Exit 0 means every requested report was captured and start/end identity did not
change. Exit 2 means a gap, unknown state, incomplete capture, transport/file error
or changed identity. Read the manifest before deciding what to do next. Existing
output directories are never overwritten. The tool does not clear the console
after a partial write; inspect that boundary manually before another operation.

## Offline verification

```bash
python3 -m unittest discover -s tools/bench -p 'test_*.py' -v
```

The tests use fake byte streams and injected clocks. They cover both reported
profiles, old unsupported firmware, missing/fragmented/mixed replies, prompt/menu
boundaries, partial writes, byte limits, strict JSON, identity changes, exact raw
preservation and the absence of explicit modem-line/reset calls. They do not open
a USB device, establish the user's module inventory, exercise a real serial driver
or qualify hardware, electrical timing or QEMU physics.
