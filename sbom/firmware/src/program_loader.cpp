// program_loader.cpp — see program_loader.h. License-clean, our own IP.
#include "program_loader.h"
#include "plc_core.h"
#include "modbus_tcp_server.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <mbedtls/base64.h>

namespace plc {

ProgramLoader program_loader;

static const char* NVS_NS   = "plc";
static const char* NVS_PROG = "prog";   // stored base64 ir-json

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

// Parse IR-json -> Instr[] in the caller's staging buffer (never the live
// program). Validates opcode range and length; returns false on anything
// malformed/oversized so the running program is left untouched.
bool ProgramLoader::apply_json(const uint8_t* json, size_t jlen,
                               Instr* out, int& out_n, uint32_t* scan_ms_out) {
  struct Reader {
    const uint8_t* data; size_t size; size_t pos = 0;
    int read() { return pos < size ? data[pos++] : -1; }
    size_t readBytes(char* dst, size_t n) {
      size_t count = 0;
      while (count < n && pos < size) dst[count++] = data[pos++];
      return count;
    }
  } reader{json, jlen};
  StaticJsonDocument<4096> doc;
  DeserializationError e = deserializeJson(doc, reader);
  if (e) return false;
  while (reader.pos < jlen) {
    const int c = reader.read();
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
  }
  const char* magic = doc["magic"] | "";
  if (strcmp(magic, "TBD-IR") != 0 || doc["magic"].as<JsonString>().size() != 8) return false;
  uint32_t period = OPENPLC_SCAN_CYCLE_MS;
  if (doc.containsKey("scan_ms")) {
    if (!doc["scan_ms"].is<uint32_t>()) return false;
    const uint32_t requested = doc["scan_ms"].as<uint32_t>();
    if (requested != 0 && requested != period) return false;
  }
  JsonArray code = doc["code"].as<JsonArray>();
  if (code.isNull()) return false;
  int n = 0;
  for (JsonVariant value : code) {
    if (!value.is<JsonArray>()) return false;
    JsonArray ins = value.as<JsonArray>();
    if (n >= cap_) return false;               // program too long for buffer
    if (ins.size() < 1 || ins.size() > 3) return false;
    for (JsonVariant operand : ins) if (!operand.is<int32_t>()) return false;
    int op = ins[0].as<int32_t>();
    int a = ins.size() > 1 ? ins[1].as<int32_t>() : 0;
    int b = ins.size() > 2 ? ins[2].as<int32_t>() : 0;
    if (op < OP_HALT || op > OP_DROP) return false;   // unknown opcode -> reject
    const bool arg = (op >= OP_PUSH_I && op <= OP_LD_IW) ||
                     (op >= OP_TON && op <= OP_FBQ);
    if ((arg && ins.size() < 2) || (op == OP_TON && ins.size() < 3)) return false;
    out[n].op = (uint8_t)op;
    out[n].a  = a;
    out[n].b  = b;
    n++;
  }
  if (validate_program(out, n)) return false;
  out_n = n;
  if (scan_ms_out) *scan_ms_out = period;
  return true;
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
  if (!cap_) return 0;
  Preferences pref;
  if (!pref.begin(NVS_NS, true)) return 0;      // read-only; ns may not exist yet
  String b64 = pref.getString(NVS_PROG, "");
  pref.end();
  if (b64.length() == 0) return 0;
  if (b64.length() >= sizeof(line_) - 17) {
    Serial.println("[LOADER] ERR stored length"); return 0;
  }

  static uint8_t buf[4096];
  size_t olen = b64_decode(b64.c_str(), b64.length(), buf, sizeof(buf));
  if (olen == 0) { Serial.println("[LOADER] ERR stored b64"); return 0; }

  static Instr staging[64];
  int n = 0;
  if (!apply_json(buf, olen, staging, n, nullptr)) {
    Serial.println("[LOADER] ERR stored ir"); return 0;
  }
  memcpy(prog, staging, n * sizeof(Instr));
  Serial.printf("[LOADER] restored validated program from NVS (%d instr); PROGRAM until explicit RUN\n", n);
  return n;
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

  static uint8_t buf[4096];
  size_t olen = b64_decode(b64, strlen(b64), buf, sizeof(buf));
  if (olen == 0) { Serial.println("[LOADER] ERR b64"); return; }

  uint32_t got_crc = crc32_le(buf, olen);
  if (got_crc != want_crc) {
    Serial.printf("[LOADER] ERR crc want=%08x got=%08x\n", want_crc, got_crc);
    return;
  }

  if (!plc_mode_allows_download(SRC_USB)) {
    Serial.printf("[LOADER] ERR mode %s (key to PROGRAM or REMOTE to download)\n", plc_mode_str());
    return;
  }
  static Instr staging[64];
  int n = 0;
  uint32_t scan_ms = 0;
  if (!apply_json(buf, olen, staging, n, &scan_ms)) {
    Serial.println("[LOADER] ERR ir");
    return;
  }

  // Serialize the second mode check, save and publication with mode commands.
  // The console runs between scans; stop and command OFF before flash I/O.
  plc_image_lock();
  if (!plc_mode_allows_download(SRC_USB)) {
    plc_image_unlock(); Serial.println("[LOADER] ERR mode changed"); return;
  }
  plc_set_mode(PLC_PROGRAM, SRC_USB, "download");
  plc_core_scan();
  // One committed NVS key includes the period in the validated JSON. Never
  // publish a live program then silently ignore a save failure.
  Preferences pref;
  if (!pref.begin(NVS_NS, false)) {
    plc_image_unlock(); Serial.println("[LOADER] ERR persist open"); return;
  }
  const bool saved = pref.putString(NVS_PROG, b64) == strlen(b64);
  pref.end();
  if (!saved) {
    plc_image_unlock(); Serial.println("[LOADER] ERR persist write"); return;
  }
  memcpy(prog, staging, n * sizeof(Instr));
  prog_n = n;
  eng.reset();
  plc_image_unlock();

  ack(prog, n, got_crc, scan_ms);
  Serial.println("[LOADER] PROGRAM; explicit RUN required");
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
