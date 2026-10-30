// Device adapter for the bounded, project-owned application image.
#pragma once
#include <Arduino.h>
#include "project_core.h"

// Accessors are used under the recursive PLC image lock by the scan, loader and
// protocol adapters. Installation is a single publication after persistence.
const project::Active& plc_project_active();
void plc_project_boot_status(project::Status status);
bool plc_project_install(const project::Definition& candidate, const char* digest, bool saved, bool restored = false);
bool plc_project_ready();
bool plc_project_can_run(bool arm);
void plc_project_refresh_expansion(); // loop owner only; never called by protocol task
void plc_project_observe_temperature(bool valid); // finite driver report, not independent bus evidence
uint32_t plc_project_generation();
void plc_project_inputs(const bool physical[plc::NDI], int32_t board_temperature);
void plc_project_outputs(bool enabled, bool physical[project::HardwareOutputs]);
bool plc_project_output_mapped(uint8_t physical);
uint8_t plc_project_di(uint8_t logical);
uint8_t plc_project_qx(uint8_t logical);
int32_t plc_project_word(uint8_t logical);
bool plc_project_memory(uint8_t logical);
bool plc_project_set_parameter(uint8_t logical, int32_t value);
// Source addresses are numeric dotted-octet order, e.g.192.0.2.10 =0xc000020a.
// Callers must use the real peer context. Routes with no peer identity must reject
// point access when enabled, rather than inventing a fallback host address.
// These additional gates never override authentication, mode or writability.
bool plc_project_host_acl_enabled();
bool plc_project_host_allowed(uint32_t ipv4);
bool plc_project_point_allowed(uint32_t ipv4, point_policy::Kind kind, uint16_t index, bool write);
bool plc_project_range_allowed(uint32_t ipv4, point_policy::Kind kind, uint16_t first, uint16_t count, bool write);
bool plc_project_all_points_allowed(uint32_t ipv4, bool write);
// Called only after acquisition refresh, before the normal solve. Explicit mode
// requests and faults cancel pending startup; a completed request never rearms.
void plc_project_startup_poll();
void plc_project_startup_cancel(const char* reason);
String plc_project_json();
