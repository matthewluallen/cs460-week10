// plc_tags.h - the tag/point database: the single source of truth that binds a
// human-readable name to a Modbus address and a physical meaning. The USB console,
// the web HMI, and the audit log all resolve points through this table.
#pragma once
#include <Arduino.h>
#include "plc_point_map.h"

enum TagKind : uint8_t { TK_COIL, TK_DI, TK_REG };  // REG = FC3 holding register image

struct TagDef {
    const char* name;
    TagKind     kind;
    uint16_t    addr;      // coil bit address, DI index (0-7), or register index
    const char* unit;
    bool        writable;  // named UI write policy; raw lab Modbus remains writable
    bool        forceable; // may it be forced (coils and DI only)
    const char* desc;
    bool        signed_value;
};

inline int32_t plc_tag_decode(const TagDef* t, uint16_t value) {
    return t->signed_value ? int16_t(value) : int32_t(value);
}

extern const TagDef PLC_TAGS[];
extern const int    PLC_TAG_COUNT;

// Resolve a tag by name (case-insensitive). Returns nullptr if not found.
const TagDef* plc_tag_find(const char* name);

// Reverse lookup: the tag bound to a coil bit / DI index / register index.
const TagDef* plc_tag_for(TagKind kind, uint16_t addr);
