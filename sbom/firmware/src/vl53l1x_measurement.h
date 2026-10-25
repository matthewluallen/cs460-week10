// VL53L1X result decoding, separate from initialization and polling policy.
// Register layout: M5Stack M5Unit-TOF ffeb3c9178d05abb9cf9850ab2635381584ee14d,
// unit_VL53L1X.hpp Data and RESULT_RANGE_STATUS (0x0089, 17 bytes).
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace vl53l1x {
enum class Quality : uint8_t { Valid, RangeError, FormatError, IoError };
struct Measurement {
    Quality quality=Quality::FormatError;
    uint8_t raw_status=0xff;
    // Signed invalid sentinel; never substitute zero for an unavailable distance.
    int32_t distance_mm=-1;
    bool valid() const { return quality==Quality::Valid; }
};
inline Measurement decode(const uint8_t* bytes,size_t size) {
    Measurement m;
    if(!bytes || size!=17)return m;
    m.raw_status=bytes[0];
    // Match the pinned manufacturer's validity predicate exactly. Clipped,
    // weak-signal and other non-OK results are not usable control measurements.
    if(m.raw_status!=9){m.quality=Quality::RangeError;return m;}
    m.distance_mm=(uint16_t(bytes[13])<<8)|bytes[14];
    m.quality=Quality::Valid;
    return m;
}
template<class Bus> Measurement read_result(Bus& bus,uint8_t address) {
    uint8_t bytes[17]{};
    if(!bus.reg16(address,0x0089,bytes,sizeof(bytes))){
        Measurement m;m.quality=Quality::IoError;return m;
    }
    return decode(bytes,sizeof(bytes));
}
} // namespace vl53l1x
