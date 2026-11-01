# Chain card — The Signal Journey (one Start press, source → consequence)

> Stage 3, chain #1. The canonical data-point-to-data-point path the whole lab is built around.
> Reviewed against: `web/public/signal_journey.html`, firmware `sbom/firmware/src/*`, the Part 3 I²C tap.

- **ID / name:** CH1 · signal-journey
- **Type:** signal journey (also the spine every trust/kill chain hangs off)
- **Consequence (worst outcome):** RELAY 1 (relay0) drives the motor **against operator intent or the
  safety interlock**, and/or the operator's view does not reflect it.
- **Status:** draft (first pass — for review)

## The links (data point → data point)
Each hop: the observation, the lens/instrument, what it **establishes** about the next layer, what it
**does not**, and the per-link mapping. "Obs?" = can a student directly observe this hop.

| # | From → To | Lens / instrument | Establishes | Does NOT establish | Technique | CWE | Obs? |
|---|---|---|---|---|---|---|---|
| 1 | operator → IN1 (di0) terminal closes; EL3H4 opto conducts | Physical · Circuit tab, the board | a field contact closed (an event happened) | **who/what** closed it (person, stuck wire, injected voltage), or that it's authorized | T0817 (Drive-by/physical) context | — (legit path) | yes |
| 2 | terminal → input image: AW9523 @0x59 reg0x00 read, bit4 0x40→0x50 | Bus · `tap.sh`/sigrok; `m5stamplc_io.cpp` DI read | the **logic now sees** di0=1 (field → input image) | that the value is **true** — the expander/bus can be forged (I²C MITM; twin QMP `qom-set di`) | **T0835** Manipulate I/O Image | CWE-345 (if trusted) | yes |
| 3 | input image → the seal-in rung solves (di0 ∧ ¬stop ∧ estop_ok …) | Logic · Logic Studio; ladder scan ~50 ms, `plc_core.cpp` | GIVEN the inputs, the program's **decision** (relay0 ← 1) | that the **program is the authorized one** (it can be altered), nor that its inputs were true | T0889 Modify Program · T0843 Program Download | — | **inferred only (see Gap G3-a)** |
| 4 | rung → the code path that runs it + writes I/O | Code · Provenance Studio; `plc_core.cpp`, `m5stamplc_io.cpp` | the **specific source line** that produced the output (audited writes) | that the running code is the **shipped/audited** code (trojan runs different code; the overflow path writes with **no audited row**) | T0862 Supply Chain · **T0835** | CWE-787/788 (overflow bypass) | audited: yes · the GAP: yes (output w/ no WRITE row) |
| 5 | code → output image: AW9523 @0x59 reg0x02 write, 0x00→0x01→0x03 | Bus · `tap.sh`; `m5stamplc_io.cpp:29-32` (`relay_write_mask`) | the controller **commanded** relay0 (+relay1) | that the **contact actually moved** — `act=1` is the latch readback, **not contact feedback** | **T0835** Manipulate I/O Image | — | yes |
| 6 | output image → RELAY 1 closes; motor (own supply via the contact) turns | Physical · Circuit tab / the board | the physical **actuation** (load energizes) if wired | that it is **safe** — the E-stop is only an *input to the logic* (TRITON: altered logic can ignore it) | T0831 Manipulation of Control · T0836 | — | yes (board) · twin models the latch |
| 7 | relay → the wire: HMI Modbus poll carries the coil; phone values arrive as MQTT | Wire · tshark/Wireshark, Node-RED | what the **supervisory layer** sees + what external clients can read/write | **who** sent a frame (Modbus/MQTT carry no identity); the HMI lamp **lags** the relay (the view ≠ the state) | T1692.001/.002 · T0832 · T0815 | CWE-306/345 | yes |

## Outside-input branches (decided 2026-11: own branch into the logic)
The inputs an attacker poisons do **not** enter at the wire (link 7) — they enter where the controller
*trusts* them, which is the **logic/code** layer. Shown as explicit branches merging into the journey:

| Branch | Merges at | What it poisons | Own chain card |
|---|---|---|---|
| Firmware update (OTA / supply-chain image) | link 4 (code) | the code that runs the scan | `trust-firmware.md` |
| Network time (NTP) | link 3 (logic) | a time-dependent decision | `trust-time.md` |
| Phone / sensor claim (MQTT) | link 3 (logic) | a word-input the rung reads | `trust-sensor.md` |
| Network write (Modbus FC5 / MQTT DCMD) | link 5 (output image) | the coil directly, bypassing links 1-4 | `trust-network-write.md` |
| Memory overflow → force mask | link 4→5 (code→output) | the output with no audited row | `trust-memory-overflow.md` |

Each branch is its own Stage-3 chain card; the Signal Journey is the clean happy-path they merge into.

## Attack-tree / bowtie view
**Top event (bowtie center):** *RELAY 1 commands the motor against intent/safety.*

**Left — threat paths (causes), each an injection at a join:**
- OR 1 — **Forge the input (link 2).** I²C/sensor-spoof or twin QMP sets di0=1 without a field event. *Barrier:* input plausibility / range-rate gate; bus integrity. *(T0835, CWE-345)*
- OR 2 — **Alter the program (link 3).** Load a rung that energizes relay0. *Barrier:* the read-back integrity gate (the RUN-enable only on a matching read-back). *(T0889/T0843)*
- OR 3 — **Run unaudited code (link 4).** Trojaned image, or the overflow that writes the force mask with no audited row. *Barrier:* signed firmware + the audited force path (provenance). *(T0862, CWE-787)*
- OR 4 — **Write over the network (link 7).** Unauthenticated Modbus FC5 / MQTT DCMD to the coil. *Barrier:* message auth + zones/conduits + segmentation. *(T1692.001, CWE-306)*
- OR 5 — **Poison a trusted side input (Part 8).** Spoof NTP/phone so the logic decides wrong. *Barrier:* authenticated time, freshness + plausibility gate. *(T0830, CWE-197/345)*

**Right — consequence paths + their barriers:**
- Motor runs unsafely → *barrier:* a protection layer the control logic **cannot rewrite** (the TRITON/CIE
  residual-risk line: "even if the firmware is fully owned, RELAY 1 stays bounded by ___").
- Operator view hides it (link 7 lag / Denial of View) → *barrier:* independent monitoring / out-of-band
  indication. *(T0815/T0832)*

## Gaps (typed) — the JOINS, not the nodes
| # | Gap | Type | Which join | Proposed fix | Pri |
|---|---|---|---|---|---|
| G3-a | The **logic solve (link 3) is not directly observable** — the student infers relay0←1 from input→output + the installed read-back; no instrument shows the live rung solving. The Signal Journey presents it as an observed lens. | G3 | 2→3→4 | Label the Logic lens as *inferred from input+output+read-back*, not observed; or add a per-rung live-solve indicator in the Studio. | P2 |
| G3-b | The **forge point at link 2** (I²C/sensor-spoof) is a separate `bus-analysis` exercise, not shown as a branch on the Signal Journey, so students don't see *where* on the journey integrity is lost. | G3 | 1→2 | Add the spoof as an explicit branch on the Signal Journey map (where a forged di0 enters). | P2 |
| G3-c | The **overflow→output join (link 4→5) with no audited row** — the chain's sharpest lesson — lives only in Parts 5/9, not as a Signal-Journey row. | G3 | 4→5 | Add an "unaudited write" variant row to the journey; cross-link to the Provenance gap. | P2 |
| G3-d | **`act=1` vs contact-moved (link 5→6)** is correctly taught as "does not establish", but the twin can never close it (no contact feedback). Ensure every place that shows `act` states this, not just Part 1. | G9 | 5→6 | Add the caveat wherever `act`/relay state is displayed (Studio, panel). | P3 |
| B13 | Links 4/7 of this chain (**unsigned OTA, supply-chain payload, NTP/Y2038**) are headline attacks with **no Threat-Map row** — the chain references techniques the cross-walk omits. | G8 | 4, 7, side | (Stage 5) add the three rows. | P1 |

## Linked cards
- Item cards: P1, P2, P3, P5 (Provenance), P6, P7, P8 — to be written in Stage 2.
- Control cards: one per bowtie barrier (input gate, read-back gate, signed-firmware+audit, network auth,
  authenticated-time/plausibility, un-rewritable protection layer) — Stage 4.

## Review notes / open questions for the team
- **Resolved (2026-11):** outside inputs get their own branch into the logic/code layer (see
  "Outside-input branches" above), each a separate trust-chain card. The Signal Journey stays the clean
  happy-path they merge into.
- Confirm the firmware source anchors for each link (link 2 DI read, link 5 `relay_write_mask`) so the
  Provenance lens cites the same lines this card does.
