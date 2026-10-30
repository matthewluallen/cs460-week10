// Closed project-v1 decoder; rejects unsupported/unknown fields, never drops settings.
#pragma once
#include "project_core.h"
#include "process_bindings_json.h"
#include <ArduinoJson.h>

namespace project {
inline size_t object_keys(JsonVariantConst value) {
    size_t count = 0;
    if (value.is<JsonObjectConst>()) for (JsonPairConst pair : value.as<JsonObjectConst>()) count += 1 + object_keys(pair.value());
    else if (value.is<JsonArrayConst>()) for (JsonVariantConst item : value.as<JsonArrayConst>()) count += object_keys(item);
    return count;
}
template<size_t N> inline bool text(JsonVariantConst value, char (&out)[N], bool required = true) {
    if (!required && value.isNull()) { out[0] = 0; return true; }
    if (!value.is<const char*>()) return false;
    const JsonString s = value.as<JsonString>();
    if (s.size() >= N || strlen(s.c_str()) != s.size()) return false;
    memcpy(out, s.c_str(), s.size() + 1); return true;
}
inline const char* decode(const uint8_t* data, size_t size, Definition& out) {
    if (!data || !size || size > PayloadCapacity) return "payload capacity (1..4096 bytes)";
    // Count structural keys, excluding string contents/escapes. ArduinoJson
    // overwrites duplicate keys; the final object count must match the wire count.
    size_t keys = 0; bool quoted = false, escaped = false;
    for (size_t i = 0; i < size; ++i) {
        const auto c = data[i];
        if (!c) return "embedded NUL";
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == ':') ++keys;
    }
    struct Reader {
        const uint8_t* data; size_t size, pos = 0;
        int read() { return pos < size ? data[pos++] : -1; }
        size_t readBytes(char* dst, size_t n) { size_t count = 0; while (count < n && pos < size) dst[count++] = data[pos++]; return count; }
    } reader{data, size};
    // Static storage avoids exhausting the ESP loop stack. Decoder has one USB
    // owner; no network path invokes it concurrently.
    static StaticJsonDocument<12288> doc;
    doc.clear();
    if (deserializeJson(doc, reader, DeserializationOption::NestingLimit(6))) return "JSON";
    while (reader.pos < size) { const int c = reader.read(); if (c != ' ' && c != '\n' && c != '\r' && c != '\t') return "trailing data"; }
    if (!doc.is<JsonObject>()) return "project object required";
    auto root = doc.as<JsonObjectConst>();
    if (root["magic"] == "TBD-IR") return "legacy TBD-IR: open/migrate a .csco project and deploy TBD-PROJECT v1";
    const unsigned root_keys = 10 + unsigned(root.containsKey("process_bindings")) + unsigned(root.containsKey("io_expansion")) +
                               unsigned(root.containsKey("startup")) + unsigned(root.containsKey("host_acl"));
    if (root.size() != root_keys || !root["magic"].is<const char*>() || root["magic"].as<JsonString>().size() != 11 ||
        root["magic"] != "TBD-PROJECT" || !root["schema"].is<unsigned>() || root["schema"].as<unsigned>() != 1) return "TBD-PROJECT schema 1 required (unknown fields refused)";
    if (!root["project_revision"].is<uint32_t>() || !root["scan_ms"].is<uint32_t>()) return "project revision/scan type";
    // This decoder is deliberately single-owner (USB loop). Keep the large
    // candidate and nested-codec wire buffer off the 16KiB loop stack.
    static Definition staged;
    static const Definition empty;
    staged=empty;
    if (!text(root["project_id"], staged.id) || !text(root["project_name"], staged.name)) return "project identity length/type";
    staged.revision = root["project_revision"]; staged.scan_ms = root["scan_ms"];
    if (!root["code"].is<JsonArrayConst>() || !root["tags"].is<JsonArrayConst>() || !root["bindings"].is<JsonObjectConst>() || !root["parameters"].is<JsonArrayConst>()) return "code/tags/bindings/parameters type";
    for (JsonVariantConst value : root["code"].as<JsonArrayConst>()) {
        if (!value.is<JsonArrayConst>() || staged.code_count >= InstructionCapacity) return "instruction shape/capacity";
        auto ins = value.as<JsonArrayConst>();
        if (!ins.size() || ins.size() > 3) return "instruction shape";
        for (auto v : ins) if (!v.is<int32_t>()) return "instruction integer type";
        const auto op = ins[0].as<int32_t>();
        if (op < plc::OP_HALT || op > plc::OP_DROP) return "opcode";
        const bool arg = (op >= plc::OP_PUSH_I && op <= plc::OP_LD_IW) || (op >= plc::OP_TON && op <= plc::OP_FBQ);
        if ((arg && ins.size() < 2) || (op == plc::OP_TON && ins.size() < 3)) return "instruction missing operand";
        auto& dest = staged.code[staged.code_count++];
        dest.op = uint8_t(op); dest.a = ins.size() > 1 ? ins[1].as<int32_t>() : 0; dest.b = ins.size() > 2 ? ins[2].as<int32_t>() : 0;
    }
    auto bindings = root["bindings"].as<JsonObjectConst>();
    if (bindings.size() != 3) return "unsupported binding settings";
    size_t expected_keys = root_keys + 3;
    if (root.containsKey("startup")) {
        auto startup = root["startup"].as<JsonObjectConst>();
        char mode[8];
        if (startup.size() != 1 || !text(startup["mode"], mode)) return "startup requires mode program or run";
        if (!strcmp(mode, "program")) staged.startup = StartupMode::Program;
        else if (!strcmp(mode, "run")) staged.startup = StartupMode::Run;
        else return "startup mode program or run required (never remote)";
        ++expected_keys;
    }
    if (root.containsKey("host_acl")) {
        auto acl = root["host_acl"].as<JsonObjectConst>();
        if (acl.size() != 2 || !acl["enabled"].is<bool>() || !acl["rules"].is<JsonArrayConst>()) return "host_acl requires enabled boolean and rules array";
        staged.host_acl.enabled = acl["enabled"];
        expected_keys += 2;
        for (JsonVariantConst value : acl["rules"].as<JsonArrayConst>()) {
            auto rule = value.as<JsonObjectConst>();
            if (rule.size() != 3 || staged.host_acl.count >= point_policy::RuleCapacity) return "host_acl rule shape/capacity (0..16)";
            char point[8], host[16], access[3];
            auto& dest = staged.host_acl.rules[staged.host_acl.count++];
            if (!text(rule["point"], point) || !point_policy::parse_point(point, dest.kind, dest.index)) return "host_acl canonical point required (use DI0..7 for COIL100..107)";
            if (!text(rule["host"], host) || !point_policy::parse_ipv4(host, dest.host)) return "host_acl canonical IPv4 host required";
            if (!text(rule["access"], access) || !(dest.access = point_policy::parse_access(access))) return "host_acl access r, w or rw required";
            expected_keys += 3;
        }
    }
    auto di = bindings["digital_inputs"].as<JsonArrayConst>();
    auto outputs = bindings["digital_outputs"].as<JsonArrayConst>();
    auto words = bindings["word_inputs"].as<JsonArrayConst>();
    auto parameters = root["parameters"].as<JsonArrayConst>();
    if (di.size() != plc::NDI || outputs.size() != plc::NQ || words.size() != plc::NIW || parameters.size() != plc::NIW) return "binding/parameter capacity";
    for (unsigned i = 0; i < plc::NDI; ++i) { if (!di[i].is<uint8_t>()) return "digital input type"; staged.digital_inputs[i] = di[i]; }
    for (unsigned i = 0; i < plc::NQ; ++i) { if (!outputs[i].is<uint8_t>()) return "digital output type"; staged.digital_outputs[i] = outputs[i]; }
    for (unsigned i = 0; i < plc::NIW; ++i) {
        char source[18];
        if (!text(words[i], source)) return "word source type";
        if (!strcmp(source, "parameter")) staged.word_inputs[i] = WordSource::Parameter;
        else if (!strcmp(source, "board_temperature")) staged.word_inputs[i] = WordSource::BoardTemperature;
        else if (!strcmp(source, "process1")) staged.word_inputs[i] = WordSource::Process1;
        else if (!strcmp(source, "process2")) staged.word_inputs[i] = WordSource::Process2;
        else if (!strcmp(source, "process3")) staged.word_inputs[i] = WordSource::Process3;
        else return "unsupported word source";
        if (!parameters[i].is<int32_t>()) return "parameter int32 required";
        staged.parameters[i] = parameters[i];
    }
    if (root.containsKey("process_bindings")) {
        static char wire[bindings::JsonCapacity];
        if (!root["process_bindings"].is<JsonObjectConst>() || measureJson(root["process_bindings"]) >= sizeof(wire)) return "process_bindings capacity/type";
        const size_t length = serializeJson(root["process_bindings"], wire, sizeof(wire));
        if (!bindings::decode(wire, length, staged.process_bindings)) return "unsupported or invalid process_bindings";
        expected_keys += object_keys(root["process_bindings"]);
    }
    if (root.containsKey("io_expansion")) {
        auto io = root["io_expansion"].as<JsonObjectConst>();
        if (io.size() != 2 || !io["address"].is<uint8_t>() || !io["revision"].is<uint8_t>()) return "io_expansion requires address and revision";
        staged.io_expansion = true; staged.io_address = io["address"]; staged.io_revision = io["revision"];
        expected_keys += 2;
    }
    for (JsonVariantConst value : root["tags"].as<JsonArrayConst>()) {
        if (!value.is<JsonObjectConst>() || staged.tag_count >= TagCapacity) return "tag object/capacity";
        auto tag = value.as<JsonObjectConst>();
        const unsigned count = 2 + unsigned(tag.containsKey("comment")) + unsigned(tag.containsKey("unit"));
        if (tag.size() != count) return "unknown tag field";
        expected_keys += count;
        auto& dest = staged.tags[staged.tag_count++]; char address[5];
        if (!text(tag["operand"], address) || !operand(address, dest.space, dest.index) || !text(tag["name"], dest.name) ||
            !text(tag["comment"], dest.comment, !tag.containsKey("comment") ? false : true) ||
            !text(tag["unit"], dest.unit, !tag.containsKey("unit") ? false : true)) return "tag field type/length/operand";
    }
    if (keys != expected_keys) return "duplicate/unknown JSON key";
    if (const char* error = validate(staged)) return error;
    out = staged; return nullptr;
}
} // namespace project
