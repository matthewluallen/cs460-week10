# Part 9 — Direct remediation and compensating controls

The vulnerable and secure firmware variants are both core teaching features.
Students can investigate a weakness, change its implementation, or contain its
exposure with a compensating control. The prepared application project is shared
between those experiments. A project cannot turn `ENABLE_CWE_DEMOS` on or off.

The two existing build environments remain. `build.sh` compiles one profile (it prints the
exact `pio run -e …` line and where the artifact lands); to then *run* it in the twin, the core
steps below use `./qemu/build-twin.sh <profile>`, which compiles **and** packages a bootable
8 MB image. Build a profile with the wrapper instead of typing the command:

```bash
./remediation/build.sh vuln      # pio run -e m5stamplc        (ENABLE_CWE_DEMOS present: teaching behavior)
./remediation/build.sh secure    # pio run -e m5stamplc_secure (ENABLE_CWE_DEMOS absent; SECURE_BUILD present)
```

Many demonstration paths select a different implementation with this guard:

```c
#ifdef ENABLE_CWE_DEMOS
    // Intentional teaching weakness.
#else
    // The particular remediation branch described below.
#endif
```

**The secure environment selects those branches; it does not establish that every
weakness, transport or deployment is secure.** Other examples use independent
macros, and some controls are mode gates or service removal rather than peer
authentication. The matrix describes what the current source does and how to test
its limits. It is not a record of successful device tests.

> **New here? Read this first.** Part 9 is one task: build the **secure** firmware, boot it,
> and watch the Part 6–8 attacks you already ran get *refused* — then write a finding that
> states the boundary (what the fix closes, and what it does **not**). The **required core** is
> the next section, "Re-run the Part 6–8 attacks" — two commands and a verifier. Everything
> under **Going deeper** below (the comparison framework and the full weakness → fix → control
> matrix) is optional reference: consult the row for the weakness you chose — you don't complete
> every row.

## Re-run the Part 6-8 attacks against the secure build

The sharpest evidence that a fix works is the attack that *stops* working. Build and boot
the secure profile, then replay the earlier parts' attacks and watch each one be refused:

```bash
./qemu/build-twin.sh secure      # compile + package the hardened 8MB image
./qemu/run.sh secure             # boot it, leave it running
./remediation/verify-secure.sh   # re-run Part 6-8 attacks; each should be REFUSED
```

`verify-secure.sh` drives the live console and HTTP services (it does not read a banner)
and checks, for each attack, that the *effect* is gone: unauthenticated OTA upload → `401`;
`login admin openplc` backdoor → refused; default `operator:1234` → refused; six bad logins
→ lockout; the Part-8 NTP step → `clk` does **not** wrap; the Part-9 `/api/tag` overflow →
relay0 stays OFF.

### Active learning

- **Predict:** before you boot the secure image, list each attack from Parts 6-8 (plus the
  `/api/tag` overflow) and write down (a) whether it will still work, and (b) *why it is
  stopped* — a **line-of-code** fix (e.g. the Y2038 `int32`→`int64` store, the overflow's
  bounds-checked copy) or an **architecture** change (e.g. no default credential ships, OTA
  now requires auth + HMAC, a new lockout state machine). They are not all the same kind.
- **Run:** `./remediation/verify-secure.sh`
- **Reconcile:** compare your two columns to the PASS/FAIL lines. A refusal proves *that
  path* is closed on *this build with this project*; it does **not** prove the transport is
  encrypted, the HMAC key is secret (it is a repo constant), or that every other route is
  safe. Note any attack you expected to fail that still worked.
- **Pair check:** have your partner name one attack and state whether it died to a single
  line or to a design change — then swap.

### New secure-build capabilities to verify (behaviorally)

The hardened build adds operator-lifecycle controls that the vulnerable build does not have.
Verify each by its behavior, not by its help text:

- **First-run enrollment.** On a secure image with blank storage no credential ships, so the
  first `passwd <new>` at the `plc>` prompt sets the operator password **without a prior
  login** (policy: ≥12 chars and ≥3 of lower/upper/digit/symbol). Verify: a short/weak
  password is rejected, a compliant one is accepted (`[AUTH] operator enrolled …`), and
  `login operator <that>` then succeeds. Changing it later requires being logged in.
- **Button-A-at-boot recovery.** Holding button A through boot clears the operator password
  and returns the device to the unenrolled state (`[AUTH] button A held at boot - operator
  password cleared`). Verify: after recovery, `login operator <old>` fails and `passwd
  <new>` re-enrolls. This is a physical-presence maintainer escape hatch, not a remote one.
- **Login lockout.** Five failed logins arm a 30-second lockout (`plc_auth.cpp`); a valid
  credential is refused during the window and accepted after it expires. The state is global
  and volatile — it does not persist across reboot or protect non-console transports.

### Caution: a string grep is not a hardening check

The `cwe`, `svcinfo`, and `proto` command output prints the **same weakness list in both
builds** — `cmd_cwe()`/`cmd_svcinfo()` in `plc_console.cpp` have no build guard, so the text
is a shared teaching reference, not proof a weakness is active. Likewise, grepping the source
for `CWE-` strings or for an `#ifdef ENABLE_CWE_DEMOS` tells you a branch *exists*, not which
branch this image *compiled*. Establish hardening **behaviorally** (admin login refused,
lockout fires on the 5th attempt, `clk` does not wrap, the overflow leaves relay0 off) or by
the secure-only salt prefix `stamplc-secure-v2:` used by `hash_pw()` — never by the presence
of a CWE string.

**That is the required core of Part 9.** Boot the secure build, run `verify-secure.sh`, and
record one finding stating the demonstrated boundary: what failed on the vulnerable build, what
the secure build refused, which positive control still passed, and what remains possible (for
example, the HMAC key is a repo constant; cleartext transport and the separate Wi-Fi OTA path
are unchanged). A build named "secure" is not a substitute for that evidence.

---

## Going deeper (optional reference)

Everything below is **optional reference depth** — you have completed the required core above.
The comparison framework and the weakness → fix → compensating-control matrix are here to
consult for the specific weakness you chose or were assigned; you do **not** complete every row.
The validation-levels section at the end records how far the current tests actually go.

## Controlled comparison experiments

Use this pattern for the weakness assigned by your instructor or chosen for your
finding. It does not add a requirement to complete every row of the matrix.

| Arm | Firmware and project | Change being evaluated |
|---|---|---|
| **A — Vulnerable baseline** | Teaching firmware and prepared project P. | Establish the intended normal operation and reproduce the selected weakness. |
| **B — External compensating control** | The **exact same firmware and project hashes** as A. | Add an external control, such as a network access rule or a restricted gateway. Repeat the normal-operation and attack probes through the relevant paths. |
| **B-device — Built-in configuration control** | The same firmware as A, with project P-policy. | Enable the optional host point policy. Retain P and P-policy and verify that only the intended configuration changed. The project/payload hash changes; this is a separate comparison from B. |
| **C — Direct remediation** | Secure comparison firmware, or the student's targeted patch, with the same project P. | Exercise the specific changed implementation and its failure behavior. A build label alone is insufficient. |

The unchanged firmware hash in A and B demonstrates that the compensating control
was not a hidden firmware patch. A compensating control can reduce reachability or
consequences while the underlying weakness remains. Name the boundary that enforces
it: device, network, gateway, host, or a supervised operating procedure.

The optional [host point policy](../docs/OPERATING-MODES-AND-ACCESS.md) is available
in **both** profiles and is disabled by default. When enabled it defaults to denial
for unlisted host/point/operation combinations and enforces independent `r`, `w`
and `rw` permissions on supported paths. It is an application-layer exposure
control, not peer authentication, encrypted transport or a replacement for the
underlying remediation. Trusted USB/RS-485 paths are outside IP rules; forwarded
serial access needs a policy at the gateway. Unscoped network management and
diagnostics are blocked while this policy is enabled. Check the operating guide's
transport matrix before selecting a probe.

For C, choose and record the same project as its comparison baseline. Do not
silently compare A with ACL disabled to C with ACL enabled and attribute every
difference to the build switch. For a direct authentication fix, a permitted IP
still needs the new credential/session check and its own positive/negative tests.

For each arm, retain:

- Source commit and effective build profile; firmware artifact hash and running
  runtime/boot identity; exported project hash and independent `project json`
  readback. An artifact hash and the runtime's ELF identity have different meanings;
  record both rather than treating them as interchangeable.
- Starting mode, loaded project, volatile parameter values, observed logical word
  values, relevant I/O baseline, force masks, latched faults, credential provisioning
  state, network route and whether this was a fresh or resumed twin.
- A **positive control** proving the intended service still works, followed by the
  narrowly scoped attack or invalid request and its expected rejection/effect.
- Actual response, audit and state readback, including outputs where relevant.
  A timeout, closed unrelated port, or failed login is not evidence that a later
  validation gate worked.
- The residual risk and a way around the compensating control that remains in
  scope, if one exists. Restore the controlled starting state before comparison.

The same payload hash identifies the same program/configuration and parameter
defaults; it does **not** prove that live state is identical. Reestablish and record
the parameter values, input scenario, force state and fault state for each arm.
Loading a project resets its VM/live parameters and clears forces; do not assume
that it also clears a latched fault or repairs the cause. Confirm observed word
values and output state after any reset before running the next probe.

Include the saved startup declaration in that baseline. Default reboot returns to
PROGRAM; an opt-in project can request RUN after validated restoration and module
readiness. That changes the consequence of power cycling or a reachable reboot
operation. Observe both the startup state and actual controller mode. Test a
cancelled or blocked startup as well as the allowed case; stopping one boot does
not remove the saved declaration for the next boot.

The full build switch changes several paths at once. Make a separate claim and
targeted probe for each CWE you discuss; do not attribute every change to a single
probe. There are no new per-CWE build flags in this project.

The repository's profile/source checks preserve the distributed teaching baseline.
A student's direct patch can intentionally change an assertion about that baseline.
Explain that changed expectation and supply a targeted regression test for the fix;
do not restore the weakness merely to obtain a green baseline-preservation check.
The build-profile label can remain `vulnerable` after a targeted patch because it
describes the selected build switch, not a grade or an audit of every code path.

## Weakness → direct fix → compensating control → evidence → limitation

Source links below refer to `sbom/firmware/src/`. **Current** means visible in the
source. **Proposed** means the named direct fix has not been implemented here.
Compensating controls are experiment options, not claims about an existing deployment.

### Credentials and sessions

| Weakness and source | Direct fix / current secure branch | Compensating control to evaluate | Evidence to collect | Residual limitation |
|---|---|---|---|---|
| CWE-798 / 259 / 1392 / 1393: hardcoded, derived and default console accounts — [plc_auth.cpp](../sbom/firmware/src/plc_auth.cpp) | **Current:** console login excludes hardcoded admin and MAC-derived service acceptance; operator requires a stored password hash. | Restrict access to the device console and management gateway to the assigned operator. | Rejected legacy credentials, plus a successful login to an already provisioned operator account. | This is console login only. Blank secure storage has no operator hash; first-use account provisioning remains unresolved. `serial_config.cpp` has separate default HTTP configuration credentials. |
| CWE-256 / 257: plaintext and recoverable operator password — [plc_auth.cpp](../sbom/firmware/src/plc_auth.cpp) | **Current:** new secure operator passwords are stored as SHA-256 with a fixed prefix; the readout no longer returns that password. | Restrict flash/NVS access and custody of test images. | With disposable fixtures, compare `userpw` and `userpwh` storage and the password readout after a controlled set operation. | The fixed prefix is not a random per-user salt or a slow password KDF. Existing teaching `userpw` is not erased by selecting secure firmware; configuration credentials remain a separate plaintext store. |
| CWE-521: deliberately weak password policy — [plc_auth.cpp](../sbom/firmware/src/plc_auth.cpp) | **Current:** secure policy requires at least 12 characters and three character classes; teaching policy accepts at most 8 uppercase letters/digits. | Use a controlled provisioning procedure and restrict who can change credentials. | Test accepted and rejected boundaries, including a strong password rejected by the teaching policy and accepted by the secure policy. | Policy acceptance does not prove secure storage, uniqueness, provisioning or resistance to offline guessing. |
| CWE-307: unlimited console login attempts — [plc_auth.cpp](../sbom/firmware/src/plc_auth.cpp) | **Current:** five failed attempts set a 30-second console lockout. | Apply gateway rate limits and limit console exposure. | After five failures, a valid provisioned credential is refused during the lockout and succeeds after it expires. | The state is global and volatile; it does not protect every transport or demonstrate persistence across reboot. Consider availability and clock-wrap behavior in further work. |
| CWE-330 / 337: predictable session-token examples — [plc_auth.cpp](../sbom/firmware/src/plc_auth.cpp), [vulnerabilities.cpp](../sbom/firmware/src/vulnerabilities.cpp) | **Current:** `plc_auth_issue_token()` uses four `esp_random()` words in the secure branch instead of the millisecond-derived teaching value. | Restrict access to the interface using the token and shorten its exposure window. | Verify the selected implementation and the consumer that actually validates the token; record issuance and rejection of a fabricated token if that consumer exists. | Different-looking samples do not prove entropy or session enforcement. Other token demonstration helpers are separate code paths; this change is not transport-wide authentication. |

### Updates and protocol writes

| Weakness and source | Direct fix / current secure branch | Compensating control to evaluate | Evidence to collect | Residual limitation |
|---|---|---|---|---|
| CWE-494: W5500 OTA accepts an image without authenticity verification — [eth_ota.cpp](../sbom/firmware/src/eth_ota.cpp) | **Current:** secure branch computes HMAC-SHA256 and aborts the update when the supplied MAC differs. **Proposed:** establish a deployment trust/key-provisioning model. | Block OTA from untrusted peers or allow updates only through a controlled maintenance path. | Use valid uploader credentials, then test missing/wrong HMAC and a known valid update separately; record response, active image identity and reboot/boot selection. | The HMAC key is a repository constant, so this demonstrates verification mechanics without excluding a reader of the source. Bytes may be staged before verification; "never written to flash" is not the claim. Boot authenticity, rollback and the Wi-Fi OTA path are not established. |
| CWE-306: unauthenticated W5500 OTA — [eth_ota.cpp](../sbom/firmware/src/eth_ota.cpp), [serial_config.cpp](../sbom/firmware/src/serial_config.cpp) | **Current:** secure W5500 upload checks HTTP Basic credentials against configuration credentials. | Restrict access to the update listener; use a protected management route. | Unauthenticated request gets `401`; correct credentials reach the next validation stage. A `401` does not prove HMAC verification. | Default configuration credentials and cleartext HTTP remain. Console operator credentials are a different store. [ota_handler.cpp](../sbom/firmware/src/ota_handler.cpp) is a separate Wi-Fi implementation without the same guard. |
| CWE-306: unauthenticated Modbus writes — [modbus_tcp_server.cpp](../sbom/firmware/src/modbus_tcp_server.cpp) | **Proposed direct fix:** authorize each peer/write through a defined secure control path. The current branch adds a `REMOTE`-mode prerequisite to FC5/6/15/16; it does not authenticate the writer. | **Current built-in control:** refuse those writes outside `REMOTE`. **External experiment:** allow only the assigned peer/network path. | Use an assigned writable point; compare actual image state and the `REFUSED` audit outside `REMOTE`, then test the allowed case in supervised `REMOTE`. | A refused write may receive a normal Modbus echo: verify state and audit. Reachable peers remain unauthenticated in `REMOTE`; trusted console mode changes are not proof of per-team isolation. |
| Part 7: untrusted build or replacement artifact — [supply-chain](../supply-chain/README.md) | **Proposed:** review and reproduce the exact source-to-artifact path and establish an authenticated release identity. | Restrict build/publishing authority and stage candidate images for review. | Match source, profile, artifact and runtime identities; test a deliberately mismatched artifact manifest in a disposable exercise. | A hash alone does not establish a trusted origin; an authentically signed malicious application can still be unsafe. No commercial update or certification claim follows from this lab. |

### Services, input handling and memory safety

| Weakness and source | Direct fix / current secure branch | Compensating control to evaluate | Evidence to collect | Residual limitation |
|---|---|---|---|---|
| CWE-489: unauthenticated debug telnet — [debug_telnet.cpp](../sbom/firmware/src/debug_telnet.cpp) | **Current:** secure branch does not start the telnet listener and returns from its handler. | Block that service at the device's network boundary while leaving the teaching image unchanged. | Teaching listener is reachable in A; the relevant external route is blocked in B; secure listener is absent in C. Include a positive check of an intended management service. | Removing this listener does not authorize HTTP, Modbus or a forwarded USB console. |
| CWE-78: unsafe diagnostic command construction — [debug_telnet.cpp](../sbom/firmware/src/debug_telnet.cpp), [vulnerabilities.cpp](../sbom/firmware/src/vulnerabilities.cpp) | **Current:** secure telnet caller filters length/characters and the telnet service is disabled. **Proposed direct API fix:** avoid shell construction and validate at every reachable entry point. | Restrict or disable diagnostic access. | Record input rejection or the simulated diagnostic log on the appropriate path; inspect the independently selected helper branch. | This ESP32 example logs/simulates the injection pattern; it does not execute a host shell. The digit/dot filter is not complete IPv4 validation, and the helper uses a separate macro. |
| CWE-22: unrestricted log-viewer path — [eth_http.cpp](../sbom/firmware/src/eth_http.cpp) | **Current:** secure Ethernet viewer rejects separators and `..` in the filename. | Restrict the viewer or route it through an allowlist of authorized log names. | A known permitted harmless fixture is readable; separator/traversal probes are rejected rather than merely returning "file not found". | Bare filenames are still resolved from the filesystem root. This is not a complete log-directory/authorization policy, and the Wi-Fi HMI is a separate implementation. |
| CWE-200 / 1273: MAC response header disclosure — [eth_http.cpp](../sbom/firmware/src/eth_http.cpp) | **Current:** secure branch omits `X-Device-MAC` from this response builder. | Limit who can query the HMI; remove unnecessary metadata at a controlled proxy. | Compare the same endpoint's headers, plus a successful response body in all arms. | The MAC is also observable elsewhere and is not a secret. Removing one header does not fix a credential derived from an identifier. |
| CWE-787 / 120: tag-label copy overwrites the adjacent force image — [plc_core.cpp](../sbom/firmware/src/plc_core.cpp), [eth_http.cpp](../sbom/firmware/src/eth_http.cpp) | **Current:** secure branch clamps the copy into a separate 32-byte buffer and does not instantiate the teaching `ForceImage` layout. | Restrict `/api/tag` at the reachable boundary while retaining the exact teaching image. | Derive the offset from the exact teaching ELF, load a known project and establish its output baseline; record force/output/audit effects. Repeat with the secure ELF and a harmless label positive control. | The endpoint still exists. Bounds checking this copy does not prove that all memory operations or routes are safe. An unconfigured/stopped runtime is not a valid positive baseline for showing application actuation. |

The offset for the overflow lesson belongs to the particular teaching build.
`sbom/firmware/tools/derive-force-offset.py` inspects the ELF; do not copy an old
payload or rely on process tag names from the previous fixed application. Use the
loaded project's symbols and the new build's layout. The secure ELF should have no
such overflow target; establish the result from that ELF rather than from a banner.

### Time and transport

| Weakness and source | Direct fix / current secure branch | Compensating control to evaluate | Evidence to collect | Residual limitation |
|---|---|---|---|---|
| CWE-197: 64-bit time truncated into signed 32-bit storage — [time_sync.cpp](../sbom/firmware/src/time_sync.cpp) | **Current:** secure branch stores and returns a 64-bit epoch. | Reject implausible time jumps at a controlled time gateway or constrain the allowed source. | Apply the same post-2038 timestamp to both profiles; teaching wraps while secure retains the supplied 64-bit value. | The supplied date is not necessarily true. This is an arithmetic fix, not authenticated time or full time-service validation. |
| CWE-345: unauthenticated time source — [time_sync.cpp](../sbom/firmware/src/time_sync.cpp) | **Proposed:** authenticated time or a separately validated trusted local clock. No such direct fix is selected by the build flag. | Restrict time traffic to a managed source and monitor drift/jumps. | Prove approved synchronization still works and an out-of-policy source cannot alter the clock through the controlled route. | Source filtering does not cryptographically authenticate time and does not remove every on-path threat. The firmware still trusts accepted replies. |
| CWE-319: cleartext HTTP/Modbus — [eth_http.cpp](../sbom/firmware/src/eth_http.cpp), [eth_ota.cpp](../sbom/firmware/src/eth_ota.cpp), [modbus_tcp_server.cpp](../sbom/firmware/src/modbus_tcp_server.cpp) | **Proposed:** authenticated encrypted transport appropriate to the service. The flag does not add TLS or encrypt Modbus. | Use an authenticated encrypted gateway/tunnel and restrict direct device access, or demonstrate the narrower protection of segmentation. | In the controlled lab, verify the protected route and test that direct bypass is blocked; distinguish confidentiality from reachability. | Segmentation alone does not encrypt traffic. Gateway termination leaves the remaining device hop in scope. |

## Validation levels and the current boundary

[Build instructions](../sbom/firmware/BUILD.md) describe the profile/preprocessor
checks, host application tests and CI compilation of both variants. Those checks
establish selected source branches and buildability. They do not prove the expected
runtime observations in the matrix.

The host loader tests inject NVS write/readback failures and model reboot using a
storage seam. They do not exercise the ESP32 flash journal, physical power loss or
flash durability. Verify the persisted project identity after an actual restart;
retain separate evidence for recovery from an interrupted write.

The CI workflow (`.github/workflows/ci.yml`) runs `qemu/smoke-test.py` in its **twin-smoke**
job, which asserts **nine lab invariant categories** — boot, i2cscan,
di0→relay0/1 control, e-stop interlock, OTA :80, HMI :8080, sigrok bus decode, NTP/Y2038
wrap, and the CWE-787 `/api/tag` overflow. Counted individually those are **16 `check()`
assertions** in CI (where `sigrok-cli` is installed and the bus leg contributes three),
dropping to 14 if the bus-decode leg cannot run. Invariant #9 is the overflow: on the
*teaching* image the unauthenticated overlong `/api/tag` write energizes relay0 by memory
corruption, and the secure build's bounds-checked copy stops it — the same effect
`verify-secure.sh` re-checks, so "the overflow no longer moves a relay" is a runnable step,
not a claim. (Count the `check(...)` calls in `qemu/smoke-test.py` yourself rather than
quoting a fixed number.)

The workflow boots a **committed historical teaching image**.
It preserves evidence for that earlier classroom release; it does not boot the two
newly compiled generic-runtime images. New-image twin and physical-bench results must
identify the exact image, project, reset state and observations separately. The twin's
embedded course-process physics also needs separation from the generic hardware
model; its implementation is not verified by these source changes.

The current trusted USB/console route supports project mode changes and loading in
both profiles. That is a deployment trust assumption, not proof that shared gateways
or teams have isolated authority. Secure first-use operator provisioning, old-secret
cleanup, common authorization across transports, update-key custody and image
release provenance remain separate work.

**Your finding should state the demonstrated boundary:** what failed in A, what
changed in B or C, which positive control still passed, and what remains possible.
A build named "secure", an HTTP error from the wrong gate, or a list of intended
fixes is not a substitute for that evidence.

---

**Next — Level 3 (Week 12): prove it.** This part *tests* that the secure build refuses each
attack. [Week 12](../week12/) goes further: it **proves** each control with the Z3 SMT solver
(the violation becomes unreachable for all inputs, not just the inputs you tried) and reasons
about the CI/CD assurance pipeline and the compensating controls for the system-of-systems
boundaries you can't prove. The engine is in [`formal-verify/`](../formal-verify/).
