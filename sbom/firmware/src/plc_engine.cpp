// plc_engine.cpp — VM implementation (our own IP). See plc_engine.h.
#include "plc_engine.h"

namespace plc {

const char* validate_program(const Instr* prog, int n) {
  if (!prog || n <= 0 || n > MAX_INSTRUCTIONS) return "length";
  int depth = 0;
  bool reachable = true;
  uint8_t fb_kind[NINST] = {};
  for (int pc = 0; pc < n; ++pc) {
    const Instr& c = prog[pc];
    int need = 0, produced = 0, limit = 0;
    bool uses_a = false, uses_b = false;
    switch (c.op) {
      case OP_HALT: break;
      case OP_PUSH_I: uses_a = true; produced = 1; break;
      case OP_LD_IX: limit = NDI; produced = 1; break;
      case OP_LD_QX: limit = NQ; produced = 1; break;
      case OP_LD_MX: limit = NMX; produced = 1; break;
      case OP_LD_IW: limit = NIW; produced = 1; break;
      case OP_ST_QX: limit = NQ; need = 1; break;
      case OP_ST_MX: limit = NMX; need = 1; break;
      case OP_AND: case OP_OR: case OP_XOR:
      case OP_GT: case OP_LT: case OP_GE: case OP_LE: case OP_EQ:
        need = 2; produced = 1; break;
      case OP_NOT: need = 1; produced = 1; break;
      case OP_DROP: need = 1; break;
      case OP_TON: case OP_TOF: uses_b = true; // fall through
      case OP_RTRIG: case OP_FTRIG: need = 1; produced = 1; limit = NINST; break;
      case OP_FBQ: limit = NINST; produced = 1; break;
      default: return "opcode";
    }
    if (limit && (c.a < 0 || c.a >= limit)) return "index";
    if (!limit && !uses_a && c.a != 0) return "unused operand";
    if (uses_b ? c.b < 0 : c.b != 0) return "preset/unused operand";
    if (c.op == OP_TON || c.op == OP_TOF || c.op == OP_RTRIG || c.op == OP_FTRIG) {
      if (fb_kind[c.a] && fb_kind[c.a] != c.op) return "FB type conflict";
      fb_kind[c.a] = c.op;
    }
    if (!reachable) continue;
    if (depth < need) return "stack underflow";
    depth = depth - need + produced;
    if (depth > STACK_CAPACITY) return "stack overflow";
    if (c.op == OP_HALT) reachable = false;
  }
  return nullptr;
}

void Engine::reset() {
  for (int i=0;i<NDI;i++) ix_[i]=false;
  for (int i=0;i<NIW;i++) iw_[i]=0;
  for (int i=0;i<NQ;i++) qx_[i]=false;
  for (int i=0;i<NMX;i++) mx_[i]=false;
  for (int i=0;i<NINST;i++) inst_[i]=FbInst();
  sp_=0;
}

bool Engine::scan(const Instr* prog, int n, uint32_t now_ms) {
  // Validate before executing even one instruction: direct callers cannot
  // partially actuate outputs or index outside an image with corrupt IR.
  if (validate_program(prog, n)) { reset(); return false; }
  sp_=0;
  for (int pc=0; pc<n; pc++) {
    const Instr& c = prog[pc];
    switch (c.op) {
      case OP_HALT: return true;
      case OP_PUSH_I: push(c.a); break;
      case OP_LD_IX: push(ix_[c.a] ? 1:0); break;
      case OP_LD_QX: push(qx_[c.a] ? 1:0); break;
      case OP_LD_MX: push(mx_[c.a] ? 1:0); break;
      case OP_ST_QX: qx_[c.a] = (pop()!=0); break;
      case OP_ST_MX: mx_[c.a] = (pop()!=0); break;
      case OP_LD_IW: push(iw_[c.a]); break;
      case OP_AND: { int32_t b=pop(),a=pop(); push((a&&b)?1:0);} break;
      case OP_OR:  { int32_t b=pop(),a=pop(); push((a||b)?1:0);} break;
      case OP_XOR: { int32_t b=pop(),a=pop(); push(((!!a)^(!!b))?1:0);} break;
      case OP_NOT: { int32_t a=pop(); push(a?0:1);} break;
      case OP_GT:  { int32_t b=pop(),a=pop(); push((a>b)?1:0);} break;
      case OP_LT:  { int32_t b=pop(),a=pop(); push((a<b)?1:0);} break;
      case OP_GE:  { int32_t b=pop(),a=pop(); push((a>=b)?1:0);} break;
      case OP_LE:  { int32_t b=pop(),a=pop(); push((a<=b)?1:0);} break;
      case OP_EQ:  { int32_t b=pop(),a=pop(); push((a==b)?1:0);} break;
      case OP_FBQ: push(inst_[c.a].q ? 1:0); break;
      case OP_DROP: (void)pop(); break;      // discard (FB-call statement result)
      case OP_TON: {                       // IEC TON: Q after IN true for PT ms
        FbInst& f = inst_[c.a];
        bool in = (pop()!=0);
        if (in) {
          if (!f.running) { f.running=true; f.t0=now_ms; }
          f.q = (now_ms - f.t0) >= (uint32_t)c.b;
        } else { f.running=false; f.q=false; }
        push(f.q ? 1:0);
      } break;
      case OP_TOF: {                        // IEC TOF: Q holds on for PT ms after IN falls
        FbInst& f = inst_[c.a];
        bool in = (pop()!=0);
        if (in) { f.running=false; f.q=true; }
        else {
          if (f.q && !f.running) { f.running=true; f.t0=now_ms; }
          if (f.running && (now_ms - f.t0) >= (uint32_t)c.b) { f.q=false; f.running=false; }
        }
        push(f.q ? 1:0);
      } break;
      case OP_RTRIG: {                      // rising-edge detector
        FbInst& f = inst_[c.a];
        bool clk = (pop()!=0);
        f.q = (clk && !f.prev);
        f.prev = clk;
        push(f.q ? 1:0);
      } break;
      case OP_FTRIG: {                      // falling-edge detector
        FbInst& f = inst_[c.a];
        bool clk = (pop()!=0);
        f.q = (!clk && f.prev);
        f.prev = clk;
        push(f.q ? 1:0);
      } break;
      default: reset(); return false;
    }
  }
  return true;
}

} // namespace plc
