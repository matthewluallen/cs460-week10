// Scan-boundary write arbiter — governs concurrency between protocols writing the same tag.
//
// The PLC image lock (plc_image_lock) already stops *torn memory* across the two cores
// (scan on core 1, Modbus comms on core 0). What it does NOT do is resolve the LOGICAL
// race: when two masters (e.g. a Modbus client and an MQTT DCMD) write the same command
// tag — SETPOINT, SOFT_ESTOP, CMD_RUN, a free register — in one scan window, the result is
// silent last-writer-wins with no record that a fight happened.
//
// This arbiter adds, for a tracked set of command tags:
//   • ownership   — a tag may name one owning protocol; a non-owner write is refused.
//   • priority    — when several sources write one tag in a window, a fixed order picks the
//                   deterministic winner (force > console > modbus > mqtt > dnp3 > other).
//   • conflict    — 2+ distinct sources on one tag in a window raise TAG_CONFLICT (counted,
//                   logged, and published to a Modbus register the Studio reads).
//
// Ladder-driven outputs (the base relays) are NOT tracked here: the scan overwrites them
// every cycle, so the ladder already wins by construction and force overrides it.
//
// Pure C++/no Arduino deps in the core logic, so it host-compiles for unit + formal tests.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace arb {

// Source of a write, as a bit so one scan window can record several on the same tag.
enum Source : uint8_t {
    SRC_NONE    = 0,
    SRC_LADDER  = 1u << 0,
    SRC_FORCE   = 1u << 1,
    SRC_CONSOLE = 1u << 2,
    SRC_MODBUS  = 1u << 3,
    SRC_MQTT    = 1u << 4,
    SRC_DNP3    = 1u << 5,
    SRC_OTHER   = 1u << 6,
};

// Priority order (highest first) used to pick the winner when a tag is contested.
// force always wins (it is the operator's deliberate override); then the local console;
// then the network protocols. Keep in sync with the Source bits.
static const uint8_t PRIORITY[] = {
    SRC_FORCE, SRC_CONSOLE, SRC_MODBUS, SRC_MQTT, SRC_DNP3, SRC_OTHER, SRC_LADDER
};

static const int MAX_TAGS = 32;   // tracked command tags (small, fixed, no heap)

// Reset all state (boot or test setup).
void reset();

// Declare a tag governed by the arbiter. `key` identifies the tag (see coil_key/reg_key).
// `owner` = 0 means "any source may write"; otherwise only that Source bit may write.
// Returns false if the tracked-tag table is full or the tag is already tracked.
bool track(uint16_t key, uint8_t owner = 0);

// Stable keys so coils and registers never collide in the same table.
inline uint16_t reg_key(uint16_t addr)  { return addr; }
inline uint16_t coil_key(uint16_t addr) { return 0x8000u | addr; }

// A protocol intends to write a tag. For an untracked tag this is a no-op returning true
// (fast path — the write proceeds unchanged). For a tracked tag it records the source for
// this scan window and enforces ownership: returns false if the tag has an owner and `src`
// is not it, meaning the caller must REFUSE the write.
bool note_write(uint16_t key, Source src);

// Call once per scan, at the boundary, to resolve the window. Returns the number of tags
// that were contested (2+ distinct sources) this scan — each also bumps the conflict
// counter and records which sources fought. Clears the window for the next scan.
int scan_boundary();

// Telemetry.
uint32_t conflict_count();        // cumulative conflicts since reset
uint16_t last_conflict_key();     // key of the most recent contested tag (0 if none)
uint8_t  last_conflict_sources(); // Source bitmask of the most recent conflict
uint8_t  last_writer(uint16_t key); // the winning Source for a tag's last resolved window

// Resolve a contested source bitmask to the single winning Source by PRIORITY.
uint8_t  winner(uint8_t source_mask);

} // namespace arb
