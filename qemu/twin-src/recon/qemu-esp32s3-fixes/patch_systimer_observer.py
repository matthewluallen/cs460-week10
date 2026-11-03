"""Install the pinned experimental non-mutating paused timer observation."""
import hashlib
from pathlib import Path
import sys

SOURCE_SHA = 'dfc4ec4f2082b3e4ea66350563842d4f5b8642a57123d75fa55444257bd7aae8'
RESULT_SHA = 'ea006f9d1819d602f618633ae10dd9081d208a824f93a31d82e057c77739dc18'
ADDITION = '/* Experimental paused observation only. Never flush/update the guest counter. */\nstatic void esp_systimer_observe_counter0(Object *obj, Visitor *v,\n                                         const char *name, void *opaque,\n                                         Error **errp)\n{\n    ESPSysTimerState *s = ESP_SYSTIMER(obj);\n    if (!runstate_check(RUN_STATE_PAUSED)) {\n        error_setg(errp, "Counter observation requires a paused guest");\n        return;\n    }\n    const ESPSysTimerCounter *c = &s->counter[0];\n    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);\n    int64_t base = c->base;\n    uint64_t stored = c->value, flushed = c->flushed;\n    uint64_t projected = stored;\n    uint64_t hz = ESP_SYSTIMER_CNT_CLK;\n    bool enabled = c->enabled;\n    if (enabled) {\n        if (base < 0 || now < base || now - base > INT64_MAX / (ESP_SYSTIMER_CNT_CLK / 1000000)) {\n            error_setg(errp, "Unsupported counter observation interval");\n            return;\n        }\n        /* Match the existing model\'s integer arithmetic, without its writes. */\n        int64_t ticks = ((now - base) * (ESP_SYSTIMER_CNT_CLK / 1000000)) / 1000;\n        projected = (stored + ticks) & ESP_SYSTIMER_52BIT_MASK;\n    }\n    if (!visit_start_struct(v, name, NULL, 0, errp)) return;\n    visit_type_int64(v, "virtual_ns", &now, errp);\n    visit_type_int64(v, "base_ns", &base, errp);\n    visit_type_uint64(v, "stored_ticks", &stored, errp);\n    visit_type_uint64(v, "flushed_ticks", &flushed, errp);\n    visit_type_uint64(v, "projected_ticks", &projected, errp);\n    visit_type_uint64(v, "clock_hz", &hz, errp);\n    visit_type_bool(v, "enabled", &enabled, errp);\n    visit_end_struct(v, NULL);\n}\n\n'

def apply(root):
    path=Path(root)/'hw/timer/esp_systimer.c'
    source=path.read_bytes()
    digest=hashlib.sha256(source).hexdigest()
    if digest==RESULT_SHA:return False
    if digest!=SOURCE_SHA:raise ValueError('Unknown systimer source; no writes performed')
    text=source.decode()
    text=text.replace('#include "qemu/osdep.h"','#include "qemu/osdep.h"\n#include "sysemu/runstate.h"')
    text=text.replace('static void esp_systimer_init(Object *obj)',ADDITION+'static void esp_systimer_init(Object *obj)')
    needle='    ESPSysTimerClass *class = ESP_SYSTIMER_GET_CLASS(obj);'
    text=text.replace(needle,needle+'\n    object_property_add(obj, "experimental-counter0", "struct",\n                        esp_systimer_observe_counter0, NULL, NULL, NULL);')
    if hashlib.sha256(text.encode()).hexdigest()!=RESULT_SHA:raise ValueError('Unexpected patch result; no writes performed')
    path.write_text(text)
    return True

if __name__=='__main__':
    print('patched' if apply(sys.argv[1]) else 'already applied')
