#include "tag_arbiter.h"

namespace arb {

namespace {
struct Tag {
    uint16_t key;
    uint8_t  owner;        // Source bit, or 0 = any
    uint8_t  window_mask;  // sources that wrote this tag in the current scan window
    uint8_t  last_winner;  // winning Source from the last resolved window
    bool     used;
};
Tag      g_tags[MAX_TAGS];
int      g_count = 0;
uint32_t g_conflicts = 0;
uint16_t g_last_conflict_key = 0;
uint8_t  g_last_conflict_sources = 0;

Tag* find(uint16_t key) {
    for (int i = 0; i < g_count; ++i)
        if (g_tags[i].used && g_tags[i].key == key) return &g_tags[i];
    return nullptr;
}
int popcount(uint8_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }
} // namespace

void reset() {
    for (int i = 0; i < MAX_TAGS; ++i) g_tags[i] = Tag{};
    g_count = 0;
    g_conflicts = 0;
    g_last_conflict_key = 0;
    g_last_conflict_sources = 0;
}

bool track(uint16_t key, uint8_t owner) {
    if (find(key)) return false;
    if (g_count >= MAX_TAGS) return false;
    Tag& t = g_tags[g_count++];
    t.key = key; t.owner = owner; t.window_mask = 0; t.last_winner = SRC_NONE; t.used = true;
    return true;
}

bool note_write(uint16_t key, Source src) {
    Tag* t = find(key);
    if (!t) return true;                      // untracked → write proceeds unchanged
    if (t->owner && src != t->owner) return false;  // ownership: refuse a non-owner write
    t->window_mask |= src;
    return true;
}

uint8_t winner(uint8_t source_mask) {
    for (unsigned i = 0; i < sizeof(PRIORITY); ++i)
        if (source_mask & PRIORITY[i]) return PRIORITY[i];
    return SRC_NONE;
}

int scan_boundary() {
    int contested = 0;
    for (int i = 0; i < g_count; ++i) {
        Tag& t = g_tags[i];
        if (!t.used || !t.window_mask) continue;
        if (popcount(t.window_mask) >= 2) {   // two or more distinct sources fought
            ++contested;
            ++g_conflicts;
            g_last_conflict_key = t.key;
            g_last_conflict_sources = t.window_mask;
        }
        t.last_winner = winner(t.window_mask);
        t.window_mask = 0;                     // clear for the next window
    }
    return contested;
}

uint32_t conflict_count()        { return g_conflicts; }
uint16_t last_conflict_key()     { return g_last_conflict_key; }
uint8_t  last_conflict_sources() { return g_last_conflict_sources; }
uint8_t  last_writer(uint16_t key) { Tag* t = find(key); return t ? t->last_winner : SRC_NONE; }

} // namespace arb
