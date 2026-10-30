// plc_engine.h — license-clean IEC 61131-3 execution engine (our own IP).
// Compact bytecode VM executed inside the firmware scan cycle. No MatIEC, no
// OpenPLC/GPL source — behavior modeled on the public IEC 61131-3 standard.
// See firmware/LOGIC_ENGINE_DESIGN.md.
#pragma once
#include <stdint.h>
#include "plc_point_map.h"

namespace plc {

// ---- Bytecode opcodes (v0). Stack is int32 (bool = 0/1). --------------------
enum Op : uint8_t {
  OP_HALT = 0,
  OP_PUSH_I,    // arg=int imm            push imm
  OP_LD_IX,     // arg=bit index          push digital input %IX
  OP_LD_QX,     // arg=bit index          push relay/coil %QX
  OP_LD_MX,     // arg=bit index          push memory bit %MX
  OP_ST_QX,     // arg=bit index          pop -> relay %QX
  OP_ST_MX,     // arg=bit index          pop -> memory bit %MX
  OP_LD_IW,     // arg=word index         push analog input %IW (int)
  OP_AND, OP_OR, OP_XOR, OP_NOT,          // bool logic on TOS
  OP_GT, OP_LT, OP_GE, OP_LE, OP_EQ,      // int compare -> bool
  OP_TON,       // arg=inst, arg2=preset  pop IN, update TON, push Q
  OP_RTRIG,     // arg=inst               pop CLK, push rising edge
  OP_FBQ,       // arg=inst               push instance Q (last value)
  OP_DROP,      // pop and discard        (toolchain: FB-call statement result)
  OP_TOF,       // arg=inst, arg2=preset  pop IN, update TOF (off-delay), push Q
  OP_FTRIG,     // arg=inst               pop CLK, push falling edge
};

struct Instr { uint8_t op; int32_t a; int32_t b; };

// I/O image sizes (bench StamPLC: 8 DI, 4 relays; expandable later)
static const int NDI = 8, NQ = 4, NMX = 32, NIW = pointmap::VM_WORD_COUNT, NINST = 16;
static const int MAX_INSTRUCTIONS = 64, STACK_CAPACITY = 64;

// Returns nullptr for valid bounded IR, otherwise a stable diagnostic. Operand
// validation covers dead code; stack validation covers the reachable prefix.
const char* validate_program(const Instr* prog, int n);

struct FbInst {         // one function-block instance (TON or R_TRIG)
  bool     q = false;   // current output
  bool     prev = false;// previous input (edge detect / TON IN latch)
  uint32_t t0 = 0;      // TON start millis
  bool     running = false;
};

class Engine {
 public:
  void reset();
  // Map current hardware inputs into the image before a scan.
  void set_input(int i, bool v)   { if (i>=0 && i<NDI)  ix_[i]=v; }
  void set_analog(int i, int32_t v){ if (i>=0 && i<NIW) iw_[i]=v; }
  void set_mem(int i, bool v)     { if (i>=0 && i<NMX) mx_[i]=v; }
  bool input(int i) const { return i>=0 && i<NDI ? ix_[i] : false; }
  int32_t analog(int i) const { return i>=0 && i<NIW ? iw_[i] : 0; }
  bool relay(int i) const         { return (i>=0 && i<NQ) ? qx_[i] : false; }
  // Function-block instance state (for the editor's online timer animation).
  bool     fbq(int i) const       { return (i>=0 && i<NINST) ? inst_[i].q : false; }
  bool     fbrunning(int i) const { return (i>=0 && i<NINST) ? inst_[i].running : false; }
  uint32_t fbt0(int i) const      { return (i>=0 && i<NINST) ? inst_[i].t0 : 0; }
  bool mem(int i) const           { return (i>=0 && i<NMX)? mx_[i] : false; }
  // Run one scan of the loaded program.
  bool scan(const Instr* prog, int n, uint32_t now_ms);
 private:
  bool ix_[NDI]={false};
  bool qx_[NQ]={false};
  bool mx_[NMX]={false};
  int32_t iw_[NIW]={0};
  FbInst inst_[NINST];
  int32_t stack_[64]; int sp_=0;
  void push(int32_t v){ if(sp_<64) stack_[sp_++]=v; }
  int32_t pop(){ return sp_>0 ? stack_[--sp_] : 0; }
};

} // namespace plc
