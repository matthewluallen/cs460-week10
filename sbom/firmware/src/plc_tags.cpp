#include "plc_tags.h"
#include <strings.h>

// Versioned names, addresses, units and encodings are generated from point_map.json.
const TagDef PLC_TAGS[] = {
#include "plc_point_tags.inc"
};
const int PLC_TAG_COUNT = sizeof(PLC_TAGS) / sizeof(PLC_TAGS[0]);

const TagDef* plc_tag_for(TagKind kind, uint16_t addr) {
    for (int i = 0; i < PLC_TAG_COUNT; i++)
        if (PLC_TAGS[i].kind == kind && PLC_TAGS[i].addr == addr) return &PLC_TAGS[i];
    return nullptr;
}

const TagDef* plc_tag_find(const char* name) {
    for (int i = 0; i < PLC_TAG_COUNT; i++) {
        if (strcasecmp(name, PLC_TAGS[i].name) == 0) return &PLC_TAGS[i];
    }
    return nullptr;
}
