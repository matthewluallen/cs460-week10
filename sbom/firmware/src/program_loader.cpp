// program_loader.cpp — see program_loader.h. License-clean, our own IP.
#include "program_loader.h"
#include "plc_core.h"
#include "modbus_tcp_server.h"
#include "plc_project.h"
#include "project_json.h"
#include <mbedtls/sha256.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <mbedtls/base64.h>

namespace plc {

ProgramLoader program_loader;

static const char* NVS_NS   = "plc";
static const char* NVS_PROG = "project_v1"; // one complete, validated project envelope

// zlib-compatible CRC-32 (little-endian, reflected poly 0xEDB88320).
uint32_t crc32_le(const uint8_t* p, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    crc ^= p[i];
    for (int k = 0; k < 8; k++)
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
  }
  return ~crc;
}

static void payload_digest(const uint8_t* data, size_t size, char (&out)[65]) {
  unsigned char digest[32];
  mbedtls_sha256(data, size, digest, 0);
  for (unsigned i = 0; i < 32; ++i) snprintf(out + i * 2, 3, "%02x", digest[i]);
}

// decode base64 -> caller buffer; returns decoded length or 0 on failure.
static size_t b64_decode(const char* b64, size_t b64len, uint8_t* out, size_t cap) {
  size_t need = 0;
  mbedtls_base64_decode(nullptr, 0, &need, (const unsigned char*)b64, b64len);
  if (need == 0 || need > cap) return 0;
  size_t olen = 0;
  if (mbedtls_base64_decode(out, cap, &olen, (const unsigned char*)b64, b64len) != 0)
    return 0;
  return olen;
}

int ProgramLoader::begin(Instr* prog, int cap) {
  cap_ = (prog && cap > 0 && cap <= MAX_INSTRUCTIONS) ? cap : 0;
  len_ = 0; discard_ = false;
  plc_project_boot_status(project::Status::Unconfigured);
  if (!cap_) {plc_project_boot_status(project::Status::Invalid); return 0;}
  memset(prog, 0, cap_ * sizeof(Instr));
  Preferences pref;
  if (!pref.begin(NVS_NS, false)) {
    plc_project_boot_status(project::Status::Invalid);
    Serial.println("[LOADER] ERR project storage unavailable; PROGRAM"); return 0;
  }
  const bool present = pref.isKey(NVS_PROG);
  const bool legacy = pref.isKey("prog");
  static uint8_t buf[project::PayloadCapacity];
  const size_t stored = present ? pref.getBytesLength(NVS_PROG) : 0;
  const size_t size = stored && stored <= sizeof(buf) && pref.getBytes(NVS_PROG, buf, stored) == stored ? stored : 0;
  pref.end();
  if (!present) {
    if (legacy) {
      plc_project_boot_status(project::Status::Invalid);
      Serial.println("[LOADER] legacy program requires migration: open a .csco project and deploy TBD-PROJECT v1");
    } else Serial.println("[LOADER] UNCONFIGURED; open and load a project before RUN");
    return 0;
  }
  static project::Definition staging;
  char digest[65];
  const char* error = size ? project::decode(buf, size, staging) : "stored project blob type/length/read";
  if (error || staging.code_count > unsigned(cap_)) {
    plc_project_boot_status(project::Status::Invalid);
    Serial.printf("[LOADER] INVALID_PROJECT %s; PROGRAM, no fallback\n", error ? error : "program capacity"); return 0;
  }
  payload_digest(buf, size, digest);
  if (!plc_project_install(staging, digest, true, true)) {
    plc_project_boot_status(project::Status::Invalid); return 0;
  }
  memcpy(prog, staging.code, staging.code_count * sizeof(Instr));
  Serial.printf("[LOADER] restored project %s sha256=%s; %s\n", staging.id, digest,
      staging.startup == project::StartupMode::Run ? "PROGRAM; startup RUN pending readiness (30s maximum)" : "PROGRAM until explicit RUN");
  return int(staging.code_count);
}

void ProgramLoader::ack(const Instr* prog, int n, uint32_t crc, uint32_t scan_ms) {
  Serial.printf("[LOADER] OK instr=%d crc=%08x scan_ms=%u\n", n, crc, scan_ms);
  for (int i = 0; i < n; i++)
    Serial.printf("[LOADER] D %d %d %ld %ld\n", i, prog[i].op,
                  (long)prog[i].a, (long)prog[i].b);
  Serial.println("[LOADER] END");
}

void ProgramLoader::handle_line(Engine& eng, Instr* prog, int& prog_n, bool* applied) {
  // Expect: PLCLOAD <crc32hex> <base64>
  if (strncmp(line_, "PLCLOAD ", 8) != 0) return;   // not for us; ignore quietly
  char* p = line_ + 8;
  char* sp = strchr(p, ' ');
  if (!sp || sp - p != 8 || !sp[1]) { Serial.println("[LOADER] ERR frame"); return; }
  for (char* h = p; h < sp; ++h) {
    if (!((*h >= '0' && *h <= '9') || (*h >= 'a' && *h <= 'f') || (*h >= 'A' && *h <= 'F'))) {
      Serial.println("[LOADER] ERR frame crc"); return;
    }
  }
  *sp = 0;
  uint32_t want_crc = (uint32_t)strtoul(p, nullptr, 16);
  char* b64 = sp + 1;

  static uint8_t buf[project::PayloadCapacity];
  size_t olen = b64_decode(b64, strlen(b64), buf, sizeof(buf));
  if (!olen) { Serial.println("[LOADER] ERR base64/payload capacity (4096 bytes)"); return; }
  uint32_t got_crc = crc32_le(buf, olen);
  if (got_crc != want_crc) {
    Serial.printf("[LOADER] ERR crc want=%08x got=%08x\n", want_crc, got_crc); return;
  }
  plc_project_startup_cancel("project_download");
  if (!plc_mode_allows_download(SRC_USB)) {
    Serial.printf("[LOADER] ERR mode %s (PROGRAM or REMOTE required)\n", plc_mode_str()); return;
  }
  static project::Definition staging;
  if (const char* error = project::decode(buf, olen, staging)) {
    Serial.printf("[LOADER] ERR %s\n", error); return;
  }
  if (staging.code_count > unsigned(cap_)) {Serial.println("[LOADER] ERR program capacity"); return;}
  char digest[65]; payload_digest(buf, olen, digest);
  plc_image_lock();
  if (!plc_mode_allows_download(SRC_USB) || plc_project_generation() == UINT32_MAX) {
    plc_image_unlock(); Serial.println("[LOADER] ERR mode changed or generation exhausted"); return;
  }
  plc_set_mode(PLC_PROGRAM, SRC_USB, "project download");
  plc_core_scan(); // inhibit outputs before flash I/O; never hot-swap a running application
  Preferences pref;
  if (!pref.begin(NVS_NS, false)) {
    plc_image_unlock(); Serial.println("[LOADER] ERR persist open; prior project retained in PROGRAM"); return;
  }
  // NVS strings are limited to4000 bytes including the terminator. Persist the
  // decoded JSON as one blob; wire base64 must not reduce the4096-byte contract.
  const bool written = pref.putBytes(NVS_PROG, buf, olen) == olen;
  static uint8_t readback[project::PayloadCapacity];
  const bool saved = written && pref.getBytesLength(NVS_PROG) == olen &&
      pref.getBytes(NVS_PROG, readback, olen) == olen && !memcmp(readback, buf, olen);
  pref.end();
  if (!plc_project_install(staging, digest, saved)) {
    plc_image_unlock(); Serial.println("[LOADER] ERR persist/readback; prior project retained in PROGRAM"); return;
  }
  memset(prog, 0, cap_ * sizeof(Instr));
  memcpy(prog, staging.code, staging.code_count * sizeof(Instr));
  prog_n = int(staging.code_count);
  eng.reset();
  plc_clear_all_forces(SRC_USB, "new project");
  plc_image_unlock();
  ack(prog, prog_n, got_crc, staging.scan_ms);
  Serial.printf("[LOADER] PROJECT id=%s revision=%lu sha256=%s\n", staging.id, (unsigned long)staging.revision, digest);
  Serial.println("[LOADER] PROGRAM; verify project json, then explicit RUN");
  if (applied) *applied = true;
}

bool ProgramLoader::poll(Engine& eng, Instr* prog, int& prog_n) {
  bool applied = false;
  unsigned budget = 512;
  while (budget-- && Serial.available() > 0) {
    int c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line_[len_] = 0;
      if (!discard_ && len_ > 0) handle_line(eng, prog, prog_n, &applied);
      len_ = 0; discard_ = false;
      if (applied) return true;               // one program per poll
      continue;
    }
    if (discard_) continue;
    if (c < 0x20 || c > 0x7e) { discard_ = true; continue; }
    if (len_ < (int)sizeof(line_) - 1) {
      line_[len_++] = (char)c;
    } else {
      len_ = 0; discard_ = true;
      Serial.println("[LOADER] ERR line too long");
    }
  }
  return applied;
}

bool ProgramLoader::feed_line(const char* s, Engine& eng, Instr* prog, int& prog_n) {
  bool applied = false;
  if (!s || strnlen(s, sizeof(line_)) >= sizeof(line_)) {
    Serial.println("[LOADER] ERR line too long"); return false;
  }
  strcpy(line_, s);
  len_ = strlen(line_);
  if (len_ > 0) handle_line(eng, prog, prog_n, &applied);
  len_ = 0;
  return applied;
}

} // namespace plc
