# Building the two StampLC runtime profiles

The generic runtime has two required firmware profiles. Both load the same prepared
project format. **Loading a project changes the application; it does not change the
firmware's security profile.** See [the runtime contract](../../docs/GENERIC-RUNTIME.md)
for project limits and [the remediation matrix](../../remediation/README.md) for the
specific controls and remaining gaps.

| PlatformIO environment | Effective security switch | Purpose |
|---|---|---|
| `m5stamplc` | `ENABLE_CWE_DEMOS` defined; `SECURE_BUILD` absent | Preserve the intentional vulnerable behavior for investigation and remediation exercises. |
| `m5stamplc_secure` | `ENABLE_CWE_DEMOS` absent; `SECURE_BUILD` defined | Compare the existing guarded remediation branches with the teaching build. The name does not certify complete security. |

`ENABLE_PLC_VM` and `ENABLE_PLC_LOADER` remain enabled in both. There is no third
required "generic" build: application independence is common to both profiles.
`rtu_serial_only` remains a diagnostic derivative of the teaching environment.
Do not write `ENABLE_CWE_DEMOS=0` to select secure behavior: the source uses `#ifdef`,
so a definition with value zero still selects the teaching branch.

## Build from source

Install PlatformIO Core (the CI uses version 6.1.18), then build the profile you want:

```bash
python3 -m pip install 'platformio==6.1.18'
cd sbom/firmware
pio run -e m5stamplc
pio run -e m5stamplc_secure
```

The first build downloads the pinned ESP32-S3 platform/toolchain and libraries and
requires network access. The project uses the platform URL and library versions in
[`platformio.ini`](platformio.ini); its compatibility scripts verify the dependency
source they patch. Keep the compiler output and dependency resolution with any
image you intend to distribute.

Outputs are separate: `.pio/build/m5stamplc/` and
`.pio/build/m5stamplc_secure/`. Each contains its own `firmware.bin`, `firmware.elf`
and boot/partition build outputs. The configuration check prints the effective
profile and rejects contradictory, missing or mislabeled security flags. It checks
the actual PlatformIO construction environments after inheritance and `build_unflags`;
it does not validate a security control's behavior.

The experimental generated-C controller route is a separate, application-specific firmware path.
It is not the generic project-loader workflow and is not evidence that a `.csco`
project loaded into an unchanged runtime.

## Check the source and application contract

From the repository root:

```bash
python3 sbom/firmware/tests/test_build_profiles.py
python3 sbom/firmware/tests/test_generic_project.py
python3 sbom/firmware/tests/test_expansion_modules.py
python3 sbom/firmware/tests/test_network_access.py
python3 sbom/firmware/tests/test_execution_diagnostics.py
python3 -m unittest discover -s tools/bench -p 'test_*.py' -v
python3 qemu/test_plant_model.py
node --test web/public/logic/project.test.mjs web/public/logic/hardware-catalog.test.mjs web/public/logic/execution-readback.test.mjs
node web/public/logic/project-embed.mjs --check
node web/public/logic/embed.mjs --check
```

`test_build_profiles.py` checks profile configuration and preprocesses representative
guarded source branches with a host C++ preprocessor. It needs Python and `g++` or
`clang++`, not the ESP32 SDK. `test_generic_project.py` compiles and exercises the
bounded project core, actual JSON decoder, loader and controller adapters on the
host, including startup admission and project policy. It requires
OpenSSL development headers/libraries (`libssl-dev` on Debian/Ubuntu) and ArduinoJson
v6.21.6 source. It can use that version already installed under `.pio/libdeps`;
otherwise set `ARDUINOJSON_INCLUDE` to its `src` directory. CI obtains the pinned
dependency and requires the decoder checks.

`test_expansion_modules.py` compiles production discovery, acquisition, binding,
IO module and Port C/Grove code with injected bus, time, GPIO, UART and persistence
services. Its [exact coverage matrix](tests/EXPANSION-COVERAGE.md) distinguishes
native acquisition, discovery-only, catalog/bench-only and untested physical paths.
`test_network_access.py` compiles the actual Modbus, HTTP API/tag, DNP3 and IEC-104
adapters with their point-permission code under both security profiles and both
WiFi/Ethernet compile selections. Its socket, clock and project-publication services
are injected; these checks do not establish live network or hardware behavior.
Both runners need Python and `g++` and enable the undefined-behavior sanitizer.

`test_execution_diagnostics.py` exercises the production timing tracker and
controller adapter, fault-history retention and bounded serializer under both
profiles. It uses the same ArduinoJson dependency as the project tests. The
read-only bench capture tests use injected serial streams and clocks; they neither
open a serial port nor require pySerial. See the
[execution evidence guide](../../docs/EXECUTION-DIAGNOSTICS.md) for the distinction
between scheduled-start jitter, full elapsed time, the unchanged 100 ms scan-fault
section and the unchanged 8 s task-watchdog request.

The Node checks exercise project import/export/deployment, startup and host-policy
authoring, and the hardware capability catalog. The two `--check` commands verify
that the standalone Studio embeds the current project implementation and the
Structured Text exporter, parser and dependency-selection code respectively.
CI also installs the pinned Playwright/Chromium runner and exercises the actual
Studio page. A browser test using fixtures is not a physical-module test. Read
each test's output for scope. The [operating modes and access guide](../../docs/OPERATING-MODES-AND-ACCESS.md)
defines the startup and permission contract exercised by these tests.

The `generic-runtime` workflow runs the host/project/browser checks and builds
**both** PlatformIO environments from the PR's source as independent gates. Its
build artifacts retain the ELF, application, bootloader, partition table, matching
framework `boot_app0.bin`, build environment and hashes for review. A successful
build or host test is not a successful firmware boot, hardware test or remediation
probe.

The separate `generic-twin` matrix consumes those exact artifacts for each profile.
[`qemu/generic-project-smoke.py`](../../qemu/generic-project-smoke.py) verifies their
hashes and source commit, validates partition checksums/ranges and the bootloader's
DIO/8 MB/80 MHz header, and merges the documented offsets into a fresh 8 MB copy.
The original artifacts are preserved. The test loads the original prepared
[`indicator.csco`](../../web/public/projects/indicator.csco) through the real console
and checks an unconfigured PROGRAM boot, refused premature RUN,
independent project/profile/ELF readback, bad-project rejection without replacement,
explicit RUN and PROGRAM, and persisted reboot with a new explicit RUN required.
That project has no startup declaration and does not enable host point restrictions;
manual RUN after reboot remains its expected behavior. This lifecycle smoke does
not exercise opt-in reboot RUN or enabled host-policy enforcement. Their host tests
above are separate evidence, not target or hardware qualification of those features.
The job retains serial/QEMU logs and structured results, including failures. Check
the result for the exact commit; the presence of this workflow is not evidence of
a successful boot.

This smoke uses base I/O and trusted-console input forces. It requests
`TBD_NOPHYS=1` and does not rely on the twin's process-physics loop or optional
module identities. The custom model's implementation of that switch is not
independently verified here. Passing establishes execution in that recorded model;
it does not establish physical contacts, module identity/freshness/calibration,
flash power-loss durability, timing qualification or security remediation. The
existing historical `twin-smoke` job continues to test its earlier committed image.

The separate `generic-physics` matrix also consumes the exact newly built image
for **each** profile. It records the pinned QEMU executable/ROM and discovers the
model interface, checks the internal-physics injection guard and virtual-time
deadline controls, then loads an explicit reservoir project and plant configuration.
The plant's units, limits, fault state and numerical replay are also tested on the
host. Passing results apply to that recorded model interface, firmware, project
and scenario. They do not qualify physical modules or prove the reproducibility
of the custom model: the referenced QEMU patch source is still missing from the
inspected release/repository. Read each physics result and its stated scope before
treating a scenario as passed; an uploaded evidence directory can contain failures.
The [QEMU experiment guide](../../docs/QEMU-Twin.md) gives the model contract,
exact-artifact commands, fault scenarios and offline replay procedure.

## Existing prebuilt images

The committed [`prebuilt/`](prebuilt/) images and existing `qemu-twin-*` release are
earlier classroom artifacts. This source change does not rebuild or relabel them.
They must not be presented as verified generic-runtime images until a release
records the matching source commit, profile, artifacts and acceptance results.
Checking `prebuilt/SHA256SUMS.txt` establishes file integrity against that list; it
does not establish that an image implements the new project contract.

An application-only `firmware.bin` is not an 8 MB factory/twin image. Do not pass it
to `qemu/run.sh` as `FLASH_IMG` or flash it at offset zero. A twin release must merge
the matching bootloader, partition table, boot metadata and application with the
correct offsets and pad to the expected flash size. Keep a clean factory image
separate from the writable image used for an individual test run.

## Local board upload and commissioning evidence

The current authorized physical installation is tracked in the
[Claude USB handoff](../../docs/CLAUDE-USB-HANDOFF.md), using the exact two-profile
[bench candidate](../../docs/USB-BENCH-CANDIDATE.json). Follow that pinned-artifact
procedure for this bench: the source-build upload example below creates a different
build and does not preserve a backup by itself.

Begin with the [read-only capture](../../tools/bench/README.md) while the controller
is in its existing normal boot state. Identify its actual firmware and installed
project, preserve a recoverable backup, and record the two physical module models,
revisions and output wiring before replacing firmware or commanding outputs.
Initial observation does not require bootloader mode. A controller plugged into a
user's computer is available to tools running on that computer; it is not exposed
automatically to a remote/cloud workspace.

With the intended board connected locally, PlatformIO applies the project's flash
layout:

```bash
cd sbom/firmware
pio run -e m5stamplc -t upload
pio device monitor -b 115200
```

Select `-e m5stamplc_secure` explicitly when testing the secure comparison profile.
A cloud Codespace cannot directly use the USB port on your laptop. The board target
is ESP32-S3 / M5Stamp S3, 8 MB flash, DIO at 80 MHz, with the configured OTA partition
layout.

For a release candidate, retain the source commit, environment, ELF/application
hashes, boot identity, `project json` readback, starting mode and observed output
state. Demonstrate an unconfigured boot, loading and reading back two different
projects on the same runtime, explicit RUN, stopped replacement, default-PROGRAM
reboot and invalid project rejection for both profiles. Keep these runtime
observations separate from the configuration/source test results.

The next bench acceptance stage also needs the following cases in both profiles,
using the exact firmware, project, module inventory and host-policy readback. These
are acceptance requirements, not results established by the current host or QEMU
tests:

| Bench case | Evidence to retain |
|---|---|
| Explicit reboot RUN | Load the opt-in project and confirm it stays PROGRAM. Reboot; observe PROGRAM with outputs inhibited while readiness is pending, followed by the ordinary guarded RUN transition within the 30-second window. Record actual mode, `startup` state/reason/elapsed time and physical output observations. Automatic REMOTE is not supported. |
| Blocked or cancelled startup | Exercise required-module absence, wrong identity/stale data, a fault and readiness timeout. Confirm outputs remain inhibited and later recovery does not retry the consumed request. An explicit PROGRAM request must cancel a pending request. Record the saved declaration separately from this boot's cancellation. |
| Host and point rules | Use real permitted and denied peers, permitted positive controls and independent state readback. Exercise read versus write/force, named tags and raw aliases, and a denied multi-point write with no partial effects. Preserve each adapter's existing mode/profile gates, including secure Modbus's REMOTE prerequisite. |
| Network and serial boundary | Record the socket peer actually observed through the lab network/NAT/gateway, and verify denial on the enabled-policy management and unfiltered-data paths. Exercise direct trusted USB/RS-485 recovery separately; a gateway forwarding serial bytes needs its own policy. |

Use the [operating guide](../../docs/OPERATING-MODES-AND-ACCESS.md) for the exact
declaration, point identities and adapter coverage. Physical contact feedback,
electrical timing, calibration and power interruption remain separate acceptance
work. The physics matrix adds model evidence without expanding the claims of the
existing default-lifecycle smoke.

The current project lifecycle uses the trusted USB/console path in both profiles.
That path does not establish transport-wide authorization. Secure operator-account
provisioning, HTTP credentials and OTA trust still have the limitations listed in
the remediation matrix. The historical `twin-smoke` job runs an earlier committed
teaching image; its success does not validate newly compiled generic or secure
firmware. The exact-artifact `generic-twin` job has the bounded lifecycle scope
described above.
