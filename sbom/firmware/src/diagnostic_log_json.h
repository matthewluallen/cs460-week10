#pragma once
#include <ArduinoJson.h>
#include "diagnostic_log_core.h"
namespace diagnostics {
inline void status_json(JsonObject d,const diagnostics::Status& s) {
    d["capacity"]=diagnostics::Ring::Capacity;d["entry_max_bytes"]=diagnostics::Entry::MaxBytes;
    d["retained"]=s.retained;d["first_sequence"]=s.first_sequence;d["last_sequence"]=s.last_sequence;
    d["next_sequence"]=s.next_sequence;d["overwritten_min"]=s.overwritten_min;
    d["truncated_min"]=s.truncated_min;d["format_errors_min"]=s.format_errors_min;d["exhausted_min"]=s.exhausted_min;
}
}
