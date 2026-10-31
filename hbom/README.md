# Part 4 — Hardware Bill of Materials (HBOM)

If an SBOM is "what's in the code," an **HBOM** is "what's on the board." It lists the
physical parts — chips, sensors, transceivers — with their maker and part number, so you
can reason about **provenance** (is this the real part?) and **counterfeit / tamper** risk,
which an SBOM can't see.

## The worksheet

Open [`stamplc-hbom.csv`](stamplc-hbom.csv). It's pre-filled with the StampLC's components
as identified from the vendor's own driver library, but several cells say `FILL IN`.
Complete them:

- **i2c_address** — most of these chips share one I²C bus. Fill the address from the
  datasheet (and confirm it against Part 3 / the live scan in Part 1).
- **datasheet_or_nvd** — link the datasheet, or an NVD search for the part.
- **known_risk_or_cve** — a chip rarely has a "CVE," so describe the *operational* risk
  instead: what breaks if this part fails, is swapped for a counterfeit, or is tampered
  with? (e.g., a wrong RTC → bad log timestamps; a swapped power monitor → masked
  over-current.)

## Reconcile HBOM with what the bus shows

Two cross-checks that are the real skill here:

1. **Firmware's assumed BOM vs. the real board.** The StampLC's I/O is driven by an
   **AW9523B** expander and a **PI4IOE5V** input expander. The teaching firmware on
   this branch already uses the real **AW9523B** — so the "firmware assumes a generic
   **MCP23017/PCA9555** at `0x20`/`0x21` instead" counterfeit angle is **hypothetical**
   here: a worked example of the *class* of mismatch, not a defect present in this build.
   The skill is the detection: if a firmware's assumed parts didn't match the silicon,
   the relays and inputs wouldn't actually work — an HBOM-vs-reality mismatch you can
   *detect* on the bus. Note in your finding how you would tell the difference.

   Note also the **BME680/688 at `0x77`**: it sits on the secondary I²C bus (**Wire1**),
   so it is a legitimate HBOM entry that will **not** appear in the Part 3 primary-bus
   (Wire0) capture. That is expected-and-unlisted-on-that-capture, not a missing part —
   reconciling it means checking the *right* bus segment, not concluding it is absent.

2. **HBOM ↔ bus.** In Part 3 you decoded a Modbus read of **register 25**. Which HBOM
   component ultimately produces that value? Trace the chain: physical chip → firmware
   driver → register → the bytes on the wire. State what you can confirm and what you're
   inferring.

### Active learning
- **Predict** — before looking at any capture, read down the `i2c_address` column of
  your HBOM and write the set of addresses it *implies* should be on the bus (e.g.
  `0x40` INA226, `0x48` LM75B, `0x77` BME680 on Wire1, plus the ones you filled in for
  the AW9523B, RX8130 and PI4IOE5V). Note which bus (Wire0 vs Wire1) each should be on.
- **Run** — reconcile against the Part 3 evidence: `./bus-analysis/decode.sh` for the
  decoded register, and the Part 3 capture / `i2c-analyzer.html` lanes for the addresses
  actually seen. (Live form: `busstream on` on the firmware console.)
- **Reconcile** — compare predicted addresses vs observed. A match confirms the chip is
  *present and responding at that address*; it does **not** by itself confirm the part is
  genuine (a counterfeit can answer at the same address). An address that is predicted but
  absent may be on the other bus (Wire1, like `0x77`) rather than missing.
- **Pair check** — tell a partner which single address, if it answered differently than
  the HBOM predicts, would most change your trust in the board, and why.

Your HBOM finding: the completed table, plus one or two sentences on a provenance/tamper
risk you consider most consequential for *this* device, and why.
