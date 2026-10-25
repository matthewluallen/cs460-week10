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
      case OP_TON: uses_b = true; // fall through
      case OP_RTRIG: need = 1; produced = 1; limit = NINST; break;
      case OP_FBQ: limit = NINST; produced = 1; break;
      default: return "opcode";
    }
    if (limit && (c.a < 0 || c.a >= limit)) return "index";
    if (!limit && !uses_a && c.a != 0) return "unused operand";
    if (uses_b ? c.b < 0 : c.b != 0) return "preset/unused operand";
    if (c.op == OP_TON || c.op == OP_RTRIG) {
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
      case OP_RTRIG: {                      // rising-edge detector
        FbInst& f = inst_[c.a];
        bool clk = (pop()!=0);
        f.q = (clk && !f.prev);
        f.prev = clk;
        push(f.q ? 1:0);
      } break;
      default: reset(); return false;
    }
  }
  return true;
}

// Self-resetting 1 s TON blinker that toggles %MX0 -> %QX0 on each edge.
// Proves the VM (timers, edges, memory, output) with NO inputs wired.
//   IN(TON0) := NOT TON0.Q ; on rising edge of Q, MX0 := MX0 XOR 1 ; QX0 := MX0
int Engine::demo_blinker(Instr* o, int cap) {
  int n=0;
  auto emit=[&](uint8_t op,int32_t a=0,int32_t b=0){ if(n<cap){o[n].op=op;o[n].a=a;o[n].b=b;n++;} };
  emit(OP_FBQ, 0);          // push TON0.Q (previous)
  emit(OP_NOT);             // IN = NOT Q
  emit(OP_TON, 0, 1000);    // TON0(IN, PT=1000ms) -> push Q
  emit(OP_RTRIG, 1);        // rising edge of Q (inst 1) -> pulse
  emit(OP_LD_MX, 0);        // push MX0
  emit(OP_XOR);             // MX0 XOR pulse
  emit(OP_ST_MX, 0);        // MX0 := ...
  emit(OP_LD_MX, 0);        // push MX0
  emit(OP_ST_QX, 0);        // QX0 := MX0
  emit(OP_HALT);
  return n;
}

int Engine::demo_process(Instr* o, int cap) {
  int n=0;
  auto emit=[&](uint8_t op,int32_t a=0,int32_t b=0){ if(n<cap){o[n].op=op;o[n].a=a;o[n].b=b;n++;} };
  // Motor demo. Matches the lab's "relay0 := di0" and the released twin image:
  //   relay0 (motor)     = di0_start AND NOT di2_estop_in
  //   relay1 (run light) = relay0 (motor)
  //   relay3 (fault lamp) = di2_estop_in
  // di1_stop/di2_estop_in use the hardwired-NC convention of the point map.
  emit(OP_LD_IX,0); emit(OP_LD_IX,2); emit(OP_NOT); emit(OP_AND); emit(OP_ST_QX,0);  // motor
  emit(OP_LD_QX,0); emit(OP_ST_QX,1);                                                // run light
  emit(OP_LD_IX,2); emit(OP_ST_QX,3);                                                // fault/e-stop lamp
  emit(OP_HALT);
  return n;
}

} // namespace plc
