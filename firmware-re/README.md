# Part 6.5 — Firmware reverse-engineering (static analysis)

Read-only static analysis of the prebuilt teaching image
`sbom/firmware/prebuilt/firmware.bin`. Nothing here flashes or runs the device —
you are taking the shipped blob apart to see what a device *is made of* and what
it *leaks*, before any of the Part 6–8 attacks.

One command runs all four steps and narrates them:

```sh
bash firmware-re/analyze.sh
```

## The four steps (and what each establishes)

1. **`esptool image-info`** — proves it is an ESP32-S3 application image and prints
   the segment table (DROM/DRAM/IRAM/**IROM**), load addresses, entry point, and the
   build id. This is your map for step 4 and for Ghidra.
2. **`strings | grep`** — the headline finding: the default credentials
   `admin:openplc` and the CWE banners (**CWE-798** hardcoded creds, **CWE-521** weak
   password rule, **CWE-256** plaintext storage, **CWE-307** no lockout) are literally
   ASCII in the image. One `strings` run, no disassembly required.
3. **`binwalk`** — scans for embedded structures and finds a **PEM RSA private key**,
   a **PEM EC private key**, a **certificate**, the **AES S-boxes**, and an embedded
   **ELF**. A private key shipped in firmware is the same on every unit and can never
   be rotated (**CWE-798/CWE-321**).
4. **`xtensa-…-objdump`** — radare2 in the Codespace has no Xtensa plugin, so we use the
   cross-`objdump` PlatformIO already installed to disassemble the IROM region into real
   Xtensa instructions (`l32r`, `call12`, `s32i`…). To get symbols, load `firmware.elf`
   (after `pio run`) or the segments at their step-1 load addresses into **Ghidra**'s
   Xtensa loader.

## MITRE
Baked-in creds/keys → **CWE-798 / CWE-321**; recovering them from a shipped image maps
to ATT&CK for ICS **T0877 (I/O Image)** / the firmware-analysis tradecraft behind
**T1693.001 (Modify Firmware: System Firmware; formerly T0857)**. The fix is Secure Boot + encrypted flash + per-device keys —
not "change the password," because the password is in the binary.
