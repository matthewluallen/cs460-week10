#pragma once
// Called after the physical buses exist. Direct ENV compatibility is checked
// before the legacy driver can issue a reset/init write.
bool device_inventory_begin();
void device_inventory_console(const char* op, const char* argument, bool extra);
void device_inventory_scan();
// Human-readable sensor table (the `sensors` command): discovered Grove/expansion
// modules on the sensor bus (PORT.A/Wire1) with decoded engineering readings.
void device_inventory_sensors_text();
