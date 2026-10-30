// Complete project-v1 deployment over the shared USB console:
//   PLCLOAD <eight hexadecimal CRC32 digits> <base64(project JSON)>\n
// Maximum decoded payload4096 bytes. CRC is transfer integrity, not authentication.
// A validated project is persisted as one raw JSON NVS blob, read back, then published
// while stopped. No program-only fallback. Query `project json` independently
// and compare payload_sha256 before an explicit `mode run`.
#pragma once
#include <Arduino.h>
#include "plc_engine.h"
#include "project_core.h"

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
  char line_[project::FrameCapacity];

  void handle_line(Engine& eng, Instr* prog, int& prog_n, bool* applied);
  void ack(const Instr* prog, int n, uint32_t crc, uint32_t scan_ms);
};

// zlib-compatible CRC-32 (poly 0xEDB88320, reflected) — matches host zlib.crc32.
uint32_t crc32_le(const uint8_t* p, size_t n);

extern ProgramLoader program_loader;

} // namespace plc
