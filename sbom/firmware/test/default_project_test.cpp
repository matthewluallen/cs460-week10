// Host unit test: the factory default project + the network-only phone points.
// No hardware, no twin. From sbom/firmware (after any `pio run`, which fetches ArduinoJson):
//   g++ -std=c++17 -Isrc -I.pio/libdeps/m5stamplc/ArduinoJson/src test/default_project_test.cpp src/plc_engine.cpp -o /tmp/dpt
//   /tmp/dpt
// It decodes factory/default-project.json with the firmware's own decoder, then runs
// the firmware's own VM scan-by-scan through the Part 1 sequence, and checks the phone
// ingest, freshness, pinning and word-source binding.
#include "project_json.h"
#include "mqtt_sensors_core.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL: %s  (line %d)\n", #c, __LINE__); ++fails; } } while (0)

namespace {
project::Definition def;
plc::Engine eng;
bool di[8];
uint32_t clock_ms = 0;
enum { START = 0, STOP = 1, ESTOP_OK = 2, RESET = 3 };
enum { MOTOR = 0, RUN_LIGHT = 1, FAULT_LAMP = 3 };

void scan(int n = 2) {
    const int32_t params[plc::NIW]{};
    for (int i = 0; i < n; ++i) {
        project::inputs(def, eng, di, 0, params);
        CHECK(eng.scan(def.code, int(def.code_count), clock_ms += 50));
    }
}
void set(int ch, bool v) { di[ch] = v; scan(); }
void pulse(int ch) { set(ch, true); set(ch, false); }
bool out(int q) { return eng.relay(q); }
std::string slurp(const char* path) { std::ifstream f(path); std::stringstream s; s << f.rdbuf(); return s.str(); }
}

static void default_project() {
    const std::string text = slurp("factory/default-project.json");
    CHECK(!text.empty());
    const char* err = project::decode(reinterpret_cast<const uint8_t*>(text.data()), text.size(), def);
    if (err) { printf("decode error: %s\n", err); ++fails; return; }
    CHECK(def.code_count == 31);
    CHECK(def.startup == project::StartupMode::Program);
    CHECK(!strcmp(def.id, "motor-start-stop-estop"));
    const char* want[] = {"start_pb", "stop_pb", "estop_ok", "reset_pb", "motor", "run_light", "fault_lamp"};
    for (const char* name : want) {
        bool found = false;
        for (unsigned i = 0; i < def.tag_count; ++i) if (!strcmp(def.tags[i].name, name)) found = true;
        CHECK(found);
    }

    // Twin at boot: every DI reads 0, so estop_ok = 0 -> the first RUN scan latches the fault.
    eng.reset();
    scan();
    CHECK(!out(MOTOR) && out(FAULT_LAMP));
    // Holding Start while tripped does nothing; restoring estop_ok keeps the fault latched.
    set(START, true); CHECK(!out(MOTOR));
    set(ESTOP_OK, true); CHECK(!out(MOTOR) && out(FAULT_LAMP));
    // Reset while Start is still held clears the fault but must NOT start the motor.
    pulse(RESET); CHECK(!out(FAULT_LAMP) && !out(MOTOR));
    set(START, false); CHECK(!out(MOTOR));

    // Clean start: PROGRAM -> RUN resets the VM (non-retentive MX); estop_ok healthy first.
    eng.reset();
    for (bool& b : di) b = false;
    di[ESTOP_OK] = true;
    scan();
    CHECK(!out(MOTOR) && !out(RUN_LIGHT) && !out(FAULT_LAMP));
    // 1. start, then release: seal-in holds motor + run light.
    set(START, true); CHECK(out(MOTOR) && out(RUN_LIGHT));
    set(START, false); CHECK(out(MOTOR) && out(RUN_LIGHT));
    // 2. stop drops it and it stays off after stop is released.
    set(STOP, true); CHECK(!out(MOTOR) && !out(RUN_LIGHT));
    set(STOP, false); CHECK(!out(MOTOR));
    // Stop held blocks start.
    set(STOP, true); pulse(START); CHECK(!out(MOTOR)); set(STOP, false);
    // 3. run again, then E-stop trip: motor drops, fault latches.
    pulse(START); CHECK(out(MOTOR));
    set(ESTOP_OK, false); CHECK(!out(MOTOR) && !out(RUN_LIGHT) && out(FAULT_LAMP));
    // Reset while still tripped does not clear.
    pulse(RESET); CHECK(out(FAULT_LAMP));
    // 4. restoring estop_ok neither restarts nor clears the fault.
    set(ESTOP_OK, true); scan(10); CHECK(!out(MOTOR) && out(FAULT_LAMP));
    // Start is refused while the fault is latched.
    pulse(START); CHECK(!out(MOTOR));
    // 5. reset clears the fault; motor stays off.
    pulse(RESET); CHECK(!out(FAULT_LAMP) && !out(MOTOR));
    // A held (stuck) reset is one pulse only: it cannot auto-clear a later trip.
    set(RESET, true);
    set(ESTOP_OK, false); CHECK(out(FAULT_LAMP));
    set(ESTOP_OK, true); CHECK(out(FAULT_LAMP));
    set(RESET, false);
    pulse(RESET); CHECK(!out(FAULT_LAMP));
    // 6. start again runs.
    pulse(START); CHECK(out(MOTOR) && out(RUN_LIGHT));
    // A wire cut on IN3 (estop_ok = 0) is the same as a trip.
    set(ESTOP_OK, false); CHECK(!out(MOTOR) && out(FAULT_LAMP));
}

static void phone_points() {
    using pointmap::PHONE_POINTS;
    phone::State s;
    auto find = [](const char* n) { for (unsigned i = 0; i < phone::Count; ++i) if (!strcmp(PHONE_POINTS[i].name, n)) return i; return 999u; };
    const unsigned light = find("light_lux"), btn1 = find("btn1"), speed = find("speed_x100"), lat = find("lat_x1000"),
                   charging = find("charging"), heading = find("heading"), tilt = find("tilt_b");
    auto msg = [&](const char* topic, const char* body, uint32_t now) {
        return s.ingest(topic, reinterpret_cast<const uint8_t*>(body), strlen(body), now);
    };
    CHECK(msg("phone/ph-a1/light", "{\"lux\":312.6}", 1000) == phone::Verdict::Accepted);
    CHECK(s.value(light, 1000) == 313 && s.fresh(light, 1000));
    CHECK(s.fresh(light, 8999) && !s.fresh(light, 9000));      // own 8 s timer
    CHECK(s.value(light, 20000) == 313);                          // analog holds when stale
    CHECK(msg("phone/ph-a1/orient", "{\"alpha\":359.6,\"beta\":-12.4,\"gamma\":3}", 2000) == phone::Verdict::Accepted);
    CHECK(s.value(heading, 2000) == 360 && s.value(tilt, 2000) == -12);
    CHECK(!s.fresh(light, 9500) && s.fresh(tilt, 9500));        // per-point freshness
    CHECK(msg("phone/ph-a1/geo", "{\"lat\":41.879,\"lon\":-87.624,\"acc\":20,\"alt\":null,\"spd\":null,\"hdg\":null}", 3000) == phone::Verdict::Accepted);
    CHECK(s.value(lat, 3000) == 41879 && !s.points[speed].seen); // null speed stays missing
    CHECK(msg("phone/ph-a1/battery", "{\"level\":87,\"charging\":true}", 3000) == phone::Verdict::Accepted);
    CHECK(s.value(charging, 3000) == 1);
    CHECK(msg("phone/ph-a1/btn1", "{\"v\":1}", 4000) == phone::Verdict::Accepted);
    CHECK(s.value(btn1, 4000) == 1 && s.value(btn1, 12000) == 0); // momentary releases when stale
    CHECK(msg("phone/ph-a1/sound", "{\"lvl\":4}", 4000) == phone::Verdict::NoField);
    CHECK(msg("phone/bad id/light", "{\"lux\":1}", 4000) == phone::Verdict::BadId);
    CHECK(msg("other/ph-a1/light", "{\"lux\":1}", 4000) == phone::Verdict::NotPhone);
    // Pinning: other ids are refused and counted; a pin change resets to missing.
    CHECK(!s.pin("bad id") && s.pin("ph-a1"));
    CHECK(!s.points[light].seen);
    CHECK(msg("phone/ph-zz/light", "{\"lux\":9}", 5000) == phone::Verdict::OtherId && s.other_id == 1);
    CHECK(msg("phone/ph-a1/light", "{\"lux\":9}", 5000) == phone::Verdict::Accepted && s.value(light, 5000) == 9);
    s.unpin();
    CHECK(msg("phone/ph-zz/light", "{\"lux\":7}", 6000) == phone::Verdict::Accepted && !strcmp(s.last_id, "ph-zz"));
    // Spoofing is undetectable: an override is just another well-formed message.
    CHECK(msg("phone/ph-zz/light", "{\"lux\":60000}", 6100) == phone::Verdict::Accepted && s.fresh(light, 6100));

    // Word sources: a project binds phone values only where it says so.
    project::WordSource src;
    CHECK(project::phone_source_from_name("phone_light_lux", src) && project::phone_slot(src) == 2 * light);
    CHECK(project::phone_source_from_name("phone_btn1_fresh", src) && project::phone_slot(src) == 2 * btn1 + 1);
    CHECK(!project::phone_source_from_name("phone_nope", src) && !project::phone_source_from_name("phone_btn1_stale", src));
    std::string text = slurp("factory/default-project.json");
    const std::string from = "\"word_inputs\":[\"parameter\",\"parameter\"";
    const size_t at = text.find(from);
    CHECK(at != std::string::npos);
    text.replace(at, from.size(), "\"word_inputs\":[\"phone_light_lux\",\"phone_light_lux_fresh\"");
    project::Definition p;
    const char* err = project::decode(reinterpret_cast<const uint8_t*>(text.data()), text.size(), p);
    CHECK(err == nullptr);
    if (!err) {
        int32_t words[phone::WordCount];
        s.words(words, 6100);
        plc::Engine e; bool none[8]{}; const int32_t params[plc::NIW]{};
        project::inputs(p, e, none, 0, params, nullptr, words);
        CHECK(e.analog(0) == 60000 && e.analog(1) == 1);
        for (int i = 0; i < 8; ++i) CHECK(!e.input(i));           // physical image untouched
        s.words(words, 20000);
        project::inputs(p, e, none, 0, params, nullptr, words);
        CHECK(e.analog(0) == 60000 && e.analog(1) == 0);          // stale: held value, fresh 0
        CHECK(project::process_mask(p) == 0);                     // never a process binding
    }
    std::string bad = text;
    bad.replace(bad.find("phone_light_lux\""), 16, "phone_unknown_x\"");
    CHECK(project::decode(reinterpret_cast<const uint8_t*>(bad.data()), bad.size(), p) != nullptr);
}

int main() {
    default_project();
    phone_points();
    if (fails) { printf("%d check(s) FAILED\n", fails); return 1; }
    printf("default_project_test: all checks passed\n");
    return 0;
}
