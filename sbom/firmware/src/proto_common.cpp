#include "proto_common.h"
#include "modbus_tcp_server.h"
#include "plc_point_map.h"
void plc_legacy_analog_snapshot(uint16_t out[4]) {
    out[0] = modbus_server.get_register(pointmap::VIN_MV);
    out[1] = modbus_server.get_register(pointmap::CURRENT_MA);
    out[2] = modbus_server.get_register(pointmap::AIO_CH1_MV);
    out[3] = modbus_server.get_register(pointmap::AIO_CH2_MV);
}
