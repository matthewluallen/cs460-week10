// Portable, bounded application contract. No storage, network, Arduino or bus I/O.
#pragma once
#include "plc_engine.h"
#include "process_bindings_core.h"
#include "point_policy.h"
#include "project_startup.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace project {
constexpr size_t PayloadCapacity = 4096;
constexpr size_t FrameCapacity = 5500; // PLCLOAD + CRC + base64(4096) + terminator
constexpr unsigned InstructionCapacity = 32, TagCapacity = 24;
constexpr unsigned HardwareOutputs = 7; // base0..3, expansion mechanical4, SSR0=5, SSR1=6
constexpr uint32_t ScanMs = 50;
enum class Space : uint8_t { IX, QX, MX, IW };
enum class WordSource : uint8_t { Parameter, BoardTemperature, Process1, Process2, Process3 };
enum class Status : uint8_t { Unconfigured, Invalid, Ready };

struct Tag {
    Space space = Space::IX;
    uint8_t index = 0;
    char name[25]{}, comment[81]{}, unit[13]{};
};
struct Definition {
    char id[33]{}, name[49]{};
    uint32_t revision = 0, scan_ms = ScanMs;
    plc::Instr code[InstructionCapacity]{};
    unsigned code_count = 0, tag_count = 0;
    Tag tags[TagCapacity]{};
    uint8_t digital_inputs[plc::NDI]{}, digital_outputs[plc::NQ]{};
    WordSource word_inputs[plc::NIW]{};
    int32_t parameters[plc::NIW]{};
    bindings::Config process_bindings;
    bool io_expansion = false;
    uint8_t io_address = 0, io_revision = 0;
    StartupMode startup = StartupMode::Program;
    point_policy::Config host_acl;
};
static_assert(plc::NDI == 8 && plc::NQ == 4 && plc::NIW == 8 && plc::NMX == 32,
              "Project v1 requires the declared VM capacities");
static_assert(sizeof(Definition)<=6144,"Review project storage/stack budgets before expanding the schema");

inline bool printable(const char* text, size_t cap, bool empty = true) {
    size_t n = 0;
    for (; n < cap && text[n]; ++n)
        if (static_cast<unsigned char>(text[n]) < 32 || static_cast<unsigned char>(text[n]) > 126) return false;
    return n < cap && (empty || n != 0);
}
inline bool identifier(const char* text, size_t cap, bool project_id = false) {
    if (!printable(text, cap, false)) return false;
    for (size_t i = 0; text[i]; ++i) {
        const char c = text[i];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!letter && c != '_' && !((i || project_id) && c >= '0' && c <= '9') && !(project_id && c == '-')) return false;
    }
    return true;
}
inline bool same_name(const char* a, const char* b) {
    while (*a && *b) {
        const char x = (*a >= 'A' && *a <= 'Z') ? *a + ('a' - 'A') : *a;
        const char y = (*b >= 'A' && *b <= 'Z') ? *b + ('a' - 'A') : *b;
        if (x != y) return false;
        ++a; ++b;
    }
    return *a == *b;
}
inline unsigned capacity(Space space) {
    switch (space) {
    case Space::IX: return plc::NDI;
    case Space::QX: return plc::NQ;
    case Space::MX: return plc::NMX;
    case Space::IW: return plc::NIW;
    }
    return 0;
}
inline bool operand(const char* text, Space& space, uint8_t& index) {
    if (!text || strlen(text) < 3 || strlen(text) > 4) return false;
    if (text[0] == 'I' && text[1] == 'X') space = Space::IX;
    else if (text[0] == 'Q' && text[1] == 'X') space = Space::QX;
    else if (text[0] == 'M' && text[1] == 'X') space = Space::MX;
    else if (text[0] == 'I' && text[1] == 'W') space = Space::IW;
    else return false;
    if (text[2] < '0' || text[2] > '9' || (text[2] == '0' && text[3])) return false;
    unsigned n = unsigned(text[2] - '0');
    if (text[3]) {
        if (text[3] < '0' || text[3] > '9') return false;
        n = n * 10 + unsigned(text[3] - '0');
    }
    if (n >= capacity(space)) return false;
    index = uint8_t(n); return true;
}
inline const char* validate(const Definition& d) {
    if (!identifier(d.id, sizeof(d.id), true) || !printable(d.name, sizeof(d.name), false)) return "project identity";
    if (d.scan_ms != ScanMs) return "unsupported scan_ms (requires 50)";
    if (d.startup != StartupMode::Program && d.startup != StartupMode::Run) return "startup mode program or run required";
    if (const char* error = point_policy::validate(d.host_acl)) return error;
    if (!d.code_count || d.code_count > InstructionCapacity) return "instruction capacity (1..32)";
    if (const char* error = plc::validate_program(d.code, int(d.code_count))) return error;
    uint16_t inputs = 0, outputs = 0;
    for (auto index : d.digital_inputs) {
        if (index >= plc::NDI || (inputs & (1u << index))) return "digital_inputs must be a permutation of 0..7";
        inputs |= 1u << index;
    }
    for (auto index : d.digital_outputs) {
        if (index >= HardwareOutputs || (outputs & (1u << index))) return "digital_outputs require four distinct destinations 0..6";
        outputs |= 1u << index;
    }
    if (!bindings::valid(d.process_bindings)) return "invalid process_bindings";
    for (auto source : d.word_inputs) {
        if (unsigned(source) > unsigned(WordSource::Process3)) return "unsupported word source";
        if (source >= WordSource::Process1 && !d.process_bindings.inputs[unsigned(source) - unsigned(WordSource::Process1)].configured)
            return "word source requires an explicit configured process binding";
    }
    if ((outputs & 0x70) && !d.io_expansion) return "expansion output requires io_expansion identity";
    if (d.io_expansion && (d.io_address < 0x20 || d.io_address > 0x2f || d.io_revision == 255)) return "invalid io_expansion identity";
    if (!d.io_expansion && (d.io_address || d.io_revision)) return "unexpected io_expansion identity";
    if (d.tag_count > TagCapacity) return "tag capacity (0..24)";
    for (unsigned i = 0; i < d.tag_count; ++i) {
        const auto& t = d.tags[i];
        if (t.index >= capacity(t.space) || !identifier(t.name, sizeof(t.name)) ||
            !printable(t.comment, sizeof(t.comment)) || !printable(t.unit, sizeof(t.unit))) return "invalid tag";
        for (unsigned j = 0; j < i; ++j)
            if (same_name(t.name, d.tags[j].name) || (t.space == d.tags[j].space && t.index == d.tags[j].index)) return "duplicate tag name/operand";
    }
    return nullptr;
}

inline bool digest_valid(const char* digest) {
    if (!digest || strlen(digest) != 64) return false;
    for (unsigned i = 0; i < 64; ++i)
        if (!((digest[i] >= '0' && digest[i] <= '9') || (digest[i] >= 'a' && digest[i] <= 'f'))) return false;
    return true;
}

// Candidate publication is independent of persistence. Call commit only after
// the complete payload was saved and read back; failure leaves this state intact.
struct Active {
    Status status = Status::Unconfigured;
    Definition definition;
    char payload_sha256[65]{};
    uint32_t generation = 0;
    int32_t parameters[plc::NIW]{};
    bool ready() const { return status == Status::Ready; }
    bool commit(const Definition& candidate, const char* digest, bool saved) {
        if (!saved || generation == UINT32_MAX || validate(candidate) || !digest_valid(digest)) return false;
        definition = candidate;
        memcpy(parameters, candidate.parameters, sizeof(parameters));
        memcpy(payload_sha256, digest, sizeof(payload_sha256));
        ++generation; status = Status::Ready; return true;
    }
};
inline const char* status_name(Status status) {
    return status == Status::Ready ? "ready" : status == Status::Invalid ? "invalid_project" : "unconfigured";
}
inline void inputs(const Definition& d, plc::Engine& engine, const bool physical[plc::NDI],
                   int32_t board_temperature, const int32_t parameters[plc::NIW], const int32_t* process_values = nullptr) {
    for (unsigned i = 0; i < plc::NDI; ++i) engine.set_input(i, physical[d.digital_inputs[i]]);
    for (unsigned i = 0; i < plc::NIW; ++i) {
        const auto source = d.word_inputs[i];
        const int32_t value = source == WordSource::Parameter ? parameters[i] : source == WordSource::BoardTemperature ? board_temperature :
            process_values ? process_values[unsigned(source) - unsigned(WordSource::Process1)] : 0;
        engine.set_analog(i, value);
    }
    // All MX is application state. No process command or first-scan assignments.
}
inline void outputs(const Definition& d, const plc::Engine& engine, bool enabled, bool physical[HardwareOutputs]) {
    for (unsigned i = 0; i < HardwareOutputs; ++i) physical[i] = false;
    if (enabled) for (unsigned i = 0; i < plc::NQ; ++i) physical[d.digital_outputs[i]] = engine.relay(i);
}
inline uint8_t process_mask(const Definition& d) {
    uint8_t mask = 0;
    for (auto source : d.word_inputs) if (source >= WordSource::Process1 && source <= WordSource::Process3)
        mask |= 1u << (unsigned(source) - unsigned(WordSource::Process1));
    return mask;
}
inline uint16_t output_coil(uint8_t physical) {
    return physical < 4 ? pointmap::RELAY0 + physical : physical == 4 ? pointmap::EXPANSION_RELAY : physical == 5 ? pointmap::SSR1 : pointmap::SSR2;
}
} // namespace project
