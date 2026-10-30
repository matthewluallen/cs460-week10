#include "plc_tags.h"
#include "plc_project.h"
#include "plc_core.h"
#include "modbus_tcp_server.h"
#include <strings.h>

TagDef PLC_TAGS[project::TagCapacity]{};
int PLC_TAG_COUNT = 0;
void plc_tags_clear() { PLC_TAG_COUNT = 0; }
void plc_tags_install(const project::Definition& d) {
    PLC_TAG_COUNT = 0;
    for (unsigned i = 0; i < d.tag_count; ++i) {
        const auto& t = d.tags[i]; auto& tag = PLC_TAGS[i];
        tag = {t.name, TK_DI, t.index, t.unit, false, false, t.comment, false};
        switch (t.space) {
        case project::Space::IX: tag.kind = TK_DI; tag.addr = d.digital_inputs[t.index]; tag.forceable = true; break;
        case project::Space::QX: tag.kind = TK_COIL; tag.addr = project::output_coil(d.digital_outputs[t.index]); tag.forceable = true; break;
        case project::Space::MX: tag.kind = TK_MX; break;
        case project::Space::IW: tag.kind = TK_IW; tag.signed_value = true; tag.writable = d.word_inputs[t.index] == project::WordSource::Parameter; break;
        }
        ++PLC_TAG_COUNT;
    }
}

const TagDef* plc_tag_for(TagKind kind, uint16_t addr) {
    for (int i = 0; i < PLC_TAG_COUNT; i++)
        if (PLC_TAGS[i].kind == kind && PLC_TAGS[i].addr == addr) return &PLC_TAGS[i];
    return nullptr;
}

const TagDef* plc_tag_find(const char* name) {
    if (!name) return nullptr;
    for (int i = 0; i < PLC_TAG_COUNT; i++) {
        if (strcasecmp(name, PLC_TAGS[i].name) == 0) return &PLC_TAGS[i];
    }
    return nullptr;
}

int32_t plc_tag_value(const TagDef* t) {
    if (!t) return 0;
    switch (t->kind) {
    case TK_COIL: return plc_coil_live(t->addr);
    case TK_DI: return plc_di_live(uint8_t(t->addr));
    case TK_MX: return plc_project_memory(uint8_t(t->addr));
    case TK_IW: return plc_project_word(uint8_t(t->addr));
    default: return plc_tag_decode(t, modbus_server.get_register(t->addr));
    }
}
bool plc_tag_set(const TagDef* t, int32_t value) {
    if (!t || !t->writable) return false;
    if (t->kind == TK_IW) return plc_project_set_parameter(uint8_t(t->addr), value);
    if (t->kind == TK_COIL) {modbus_server.set_coil(t->addr, value != 0); return true;}
    if (t->kind == TK_REG) {modbus_server.set_register(t->addr, uint16_t(value)); return true;}
    return false;
}
String plc_json_text(const char* value) {
    String out;
    for (const char* p = value; p && *p; ++p) {
        if (*p == '"' || *p == '\\') out += '\\';
        out += *p;
    }
    return out;
}
