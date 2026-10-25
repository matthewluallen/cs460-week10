#pragma once
// Called after the physical buses exist. Direct ENV compatibility is checked
// before the legacy driver can issue a reset/init write.
bool device_inventory_begin();
void device_inventory_console(const char* op, const char* argument, bool extra);
void device_inventory_scan();
