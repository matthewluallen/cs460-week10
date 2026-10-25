# Part 3 — Hardware Bill of Materials (HBOM)

If an SBOM is "what's in the code," an **HBOM** is "what's on the board." It lists the
physical parts — chips, sensors, transceivers — with their maker and part number, so you
can reason about **provenance** (is this the real part?) and **counterfeit / tamper** risk,
which an SBOM can't see.

## The worksheet

Open [`stamplc-hbom.csv`](stamplc-hbom.csv). It's pre-filled with the StampLC's components
as identified from the vendor's own driver library, but several cells say `FILL IN`.
Complete them:

- **i2c_address** — most of these chips share one I²C bus. Fill the address from the
  datasheet (and confirm it against Part 1 / the live scan in Part 5).
- **datasheet_or_nvd** — link the datasheet, or an NVD search for the part.
- **known_risk_or_cve** — a chip rarely has a "CVE," so describe the *operational* risk
  instead: what breaks if this part fails, is swapped for a counterfeit, or is tampered
  with? (e.g., a wrong RTC → bad log timestamps; a swapped power monitor → masked
  over-current.)

## Reconcile HBOM with what the bus shows

Two cross-checks that are the real skill here:

1. **Firmware's assumed BOM vs. the real board.** The StampLC's I/O is driven by an
   **AW9523B** expander and a **PI4IOE5V** input expander. Some OpenPLC builds instead
   assume a generic **MCP23017/PCA9555** at `0x20`/`0x21`. If the firmware's assumed parts
   don't match the silicon, the relays and inputs won't actually work — an HBOM-vs-reality
   mismatch you can *detect*. Note in your finding how you would tell the difference.

2. **HBOM ↔ bus.** In Part 1 you decoded a Modbus read of **register 25**. Which HBOM
   component ultimately produces that value? Trace the chain: physical chip → firmware
   driver → register → the bytes on the wire. State what you can confirm and what you're
   inferring.

Your HBOM finding: the completed table, plus one or two sentences on a provenance/tamper
risk you consider most consequential for *this* device, and why.
