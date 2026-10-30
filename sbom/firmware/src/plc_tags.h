// Application tags come only from the installed project. Hardware channels and
// reserved diagnostic addresses remain in plc_point_map.h, without process roles.
#pragma once
#include <Arduino.h>
#include "plc_point_map.h"
#include "project_core.h"

enum TagKind : uint8_t { TK_COIL, TK_DI, TK_REG, TK_MX, TK_IW };

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

extern TagDef PLC_TAGS[project::TagCapacity];
extern int PLC_TAG_COUNT;
void plc_tags_clear();
void plc_tags_install(const project::Definition& definition);
int32_t plc_tag_value(const TagDef* tag);
bool plc_tag_set(const TagDef* tag, int32_t value);
String plc_json_text(const char* value);

// Resolve a tag by name (case-insensitive). Returns nullptr if not found.
const TagDef* plc_tag_find(const char* name);

// Reverse lookup: the tag bound to a coil bit / DI index / register index.
const TagDef* plc_tag_for(TagKind kind, uint16_t addr);
