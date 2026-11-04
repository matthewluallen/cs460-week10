# Ladder & Structured Text — language reference (TBD Logic Studio / StampLC runtime)

A practical reference for the ladder and Structured Text (ST) you write in the Logic Studio and run
on the controller (and its QEMU twin). It is deliberately small: the Studio compiles your program to
a tiny verified bytecode that runs one **scan** per cycle on the device. This page lists exactly what
that runtime supports. The authoritative source is `sbom/firmware/src/plc_engine.h` (the opcode set);
the Part 2 program walkthrough is in [`README.md`](README.md).

> New to this? In the Studio click **Examples → Motor start/stop + E-stop** to see a real program as
> both ladder and ST, and **? Help** (toolbar) for the component/term overview.

## Addresses (the I/O image)
Follows IEC 61131-3 direct addressing. Sizes are fixed by the runtime:

| Address | Meaning | Count | Read / Write |
|---|---|---|---|
| `%IX0`…`%IX7` | digital inputs (DI1–DI8) | 8 | read |
| `%QX0`…`%QX3` | relay / coil outputs (RELAY 1–4) | 4 | read + write |
| `%MX0`…`%MX31` | internal memory bits (scratch) | 32 | read + write |
| `%IW…` | analog input words | per point map | read |
| `%MW0`…`%MW15` | memory words (scratch, int) | 16 | read + write |
| `%QW0`…`%QW3` | analog output words (int, readback) | 4 | read + write |

Bits are boolean (0/1); words are signed 32-bit integers.

## Operators
- **Boolean:** `AND`, `OR`, `XOR`, `NOT` (operate on bits).
- **Comparison** (integer → bit): `>` `<` `>=` `<=` `=` .
- **Arithmetic** (int32, **saturating**; divide-by-zero yields 0): `+` `-` `*` `/` .
- **Assignment** to a coil/bit (`%QX`,`%MX`) or a word (`%MW`,`%QW`).
- Integer literals are allowed as operands.

## Function blocks (instanced — 16 instances max per program)
| Block | Call | Output |
|---|---|---|
| On-delay timer | `TON(IN, PT)` | `Q` true once `IN` held for `PT` |
| Off-delay timer | `TOF(IN, PT)` | `Q` stays true for `PT` after `IN` drops |
| Rising edge | `R_TRIG(CLK)` | `Q` true for one scan on a 0→1 of `CLK` |
| Falling edge | `F_TRIG(CLK)` | `Q` true for one scan on a 1→0 of `CLK` |
| Up/down counter | `CTUD(CU, CD, R, LD, PV)` | `QU` (and current value `CV`) |
| PID | `PID(SP, PV, KP, KI, KD, OUTMIN, OUTMAX)` | clamped `CV` |
| Clamp | `LIMIT(MN, IN, MX)` | `IN` clamped to `[MN, MX]` |
| Select | `SEL(G, IN0, IN1)` | `G ? IN1 : IN0` |

PID gains are entered as fixed-point **hundredths** — e.g. `KP = 1.50` is written as `150`
(the runtime's `PID_SCALE` is 100).

See the **Examples** projects and the Studio's **? Help** for the exact ST call syntax; this table is
the set of blocks the runtime implements.

## Ladder view
The same program renders as ladder: contacts are loads (`%IX`/`%QX`/`%MX`) in series (`AND`),
parallel (`OR`) or negated (`NOT` = normally-closed); a coil is an assignment to `%QX`/`%MX`; a
**seal-in** is a feedback branch (the coil's own `%QX` OR-ed back in); function blocks render as boxes.
Click any element and the Ladder, Structured Text and register map highlight together.

## Runtime limits (why programs stay small and analyzable)
- **≤ 64 instructions** per program; **16** function-block instances; operand stack depth 64.
- **One scan per cycle**: inputs are sampled, the program runs top to bottom, outputs are written.
- The Studio **validates** the program (bounds + stack) before it will enable **Load application ▸**,
  and the controller re-validates on load — invalid or oversized programs are refused.

## Worked example
`Examples → Motor start/stop + E-stop` implements a motor seal-in with a normally-closed E-stop whose
trip latches a fault, a Reset, and no automatic restart. Its wired points carry real IEC designations
(e.g. `start_pb` → `%IX0.0`/`-S1`, `motor` → `%QX0.0`/`-K1`); run `show motor` on the `plc>` console to
see them. The rung logic and the "what is / isn't a jog" discussion are in [`README.md`](README.md).

## See also
- Part 2 program walkthrough: [`ladder/README.md`](README.md)
- The runtime opcode set (authoritative): `sbom/firmware/src/plc_engine.h`
- IEC 61131-3 (the standard these languages and addresses come from)
