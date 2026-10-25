// program_loader.h — receive an authored IR program over the USB-C serial
// console, validate typed IR, operands and stack, persist it and install it in
// PROGRAM at a scan boundary (explicit RUN required), so it survives a
// reboot. License-clean, our own IP. See firmware/LOGIC_ENGINE_DESIGN.md.
//
// Wire frame (one text line, coexists with [IO] telemetry on the same port):
//   PLCLOAD <crc32hex> <base64(ir-json)>\n
// where ir-json is exactly what toolbase/plc_ir.py:to_json() emits, and crc32hex
// is the little-endian zlib CRC-32 of those json bytes.
//
// Device reply (host verifies it is running exactly what was sent):
//   [LOADER] OK instr=<n> crc=<hex> scan_ms=<ms>
//   [LOADER] D <i> <op> <a> <b>        (one per instruction)
//   [LOADER] END
// or on failure:
//   [LOADER] ERR <reason>
#pragma once
#include <Arduino.h>
#include "plc_engine.h"

namespace plc {

class ProgramLoader {
 public:
  // Attach the program buffer and load the last-good program from NVS (if any).
  // Returns the instruction count loaded from NVS, or 0 when none is stored.
  int begin(Instr* prog, int cap);

  // Call every loop(). Non-blocking. Returns true when a new program was
  // received, validated and applied (engine reset, buffer + count updated).
  bool poll(Engine& eng, Instr* prog, int& prog_n);

  // Process one already-read line (e.g. from a shared console reader).
  // Returns true when a program was received, validated and applied.
  bool feed_line(const char* line, Engine& eng, Instr* prog, int& prog_n);

 private:
  int  cap_  = 0;
  int  len_  = 0;
  bool discard_ = false;
  char line_[3072];

  bool apply_json(const uint8_t* json, size_t jlen, Instr* prog, int& out_n, uint32_t* scan_ms_out);
  void handle_line(Engine& eng, Instr* prog, int& prog_n, bool* applied);
  void ack(const Instr* prog, int n, uint32_t crc, uint32_t scan_ms);
};

// zlib-compatible CRC-32 (poly 0xEDB88320, reflected) — matches host zlib.crc32.
uint32_t crc32_le(const uint8_t* p, size_t n);

extern ProgramLoader program_loader;

} // namespace plc
