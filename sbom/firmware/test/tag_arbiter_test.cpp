// Host unit test for the scan-boundary arbiter. Builds with plain g++, no hardware:
//   g++ -std=c++17 -I../src tag_arbiter_test.cpp ../src/tag_arbiter.cpp -o t && ./t
#include "tag_arbiter.h"
#include <cstdio>
#include <cassert>
using namespace arb;

static int fails = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s  (line %d)\n", #c, __LINE__); ++fails; } }while(0)
static int popcnt(uint8_t v){ int n=0; while(v){ n += v&1; v >>= 1; } return n; }

int main() {
    const uint16_t SETPOINT = reg_key(30);
    const uint16_t SOFT_ESTOP = coil_key(8);
    const uint16_t CMD_RUN = coil_key(9);

    reset();
    CHECK(track(SETPOINT));                    // any source may write
    CHECK(track(SOFT_ESTOP, SRC_CONSOLE));     // owned by the local console
    CHECK(track(CMD_RUN));
    CHECK(!track(SETPOINT));                    // duplicate refused

    // 1) single source → no conflict, last_writer is that source
    CHECK(note_write(SETPOINT, SRC_MODBUS));
    CHECK(scan_boundary() == 0);
    CHECK(conflict_count() == 0);
    CHECK(last_writer(SETPOINT) == SRC_MODBUS);

    // 2) two masters fight over SETPOINT in one window → one conflict, MODBUS wins by priority
    CHECK(note_write(SETPOINT, SRC_MQTT));
    CHECK(note_write(SETPOINT, SRC_MODBUS));
    CHECK(scan_boundary() == 1);
    CHECK(conflict_count() == 1);
    CHECK(last_conflict_key() == SETPOINT);
    CHECK((last_conflict_sources() & SRC_MQTT) && (last_conflict_sources() & SRC_MODBUS));
    CHECK(last_writer(SETPOINT) == SRC_MODBUS);          // MODBUS > MQTT

    // 3) window cleared: a lone write next scan is not a conflict
    CHECK(note_write(SETPOINT, SRC_MQTT));
    CHECK(scan_boundary() == 0);
    CHECK(conflict_count() == 1);                        // unchanged
    CHECK(last_writer(SETPOINT) == SRC_MQTT);

    // 4) ownership: SOFT_ESTOP is owned by the console; a Modbus write is refused
    CHECK(note_write(SOFT_ESTOP, SRC_MODBUS) == false);  // caller must refuse
    CHECK(note_write(SOFT_ESTOP, SRC_CONSOLE) == true);
    CHECK(scan_boundary() == 0);                         // only the owner wrote → no conflict
    CHECK(last_writer(SOFT_ESTOP) == SRC_CONSOLE);

    // 5) force beats everything in a contested window
    CHECK(note_write(CMD_RUN, SRC_MQTT));
    CHECK(note_write(CMD_RUN, SRC_FORCE));
    CHECK(scan_boundary() == 1);
    CHECK(last_writer(CMD_RUN) == SRC_FORCE);

    // 6) untracked tag → note_write is a no-op that allows the write, never a conflict
    CHECK(note_write(reg_key(999), SRC_DNP3) == true);
    CHECK(scan_boundary() == 0);

    // 7) three-way fight counts once and records all three sources
    reset();
    CHECK(track(SETPOINT));
    note_write(SETPOINT, SRC_MODBUS);
    note_write(SETPOINT, SRC_MQTT);
    note_write(SETPOINT, SRC_DNP3);
    CHECK(scan_boundary() == 1);
    CHECK(conflict_count() == 1);
    CHECK(popcnt(last_conflict_sources()) == 3);
    CHECK(last_writer(SETPOINT) == SRC_MODBUS);          // highest priority of the three

    printf(fails ? "\n%d CHECK(s) FAILED\n" : "\nALL ARBITER TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
