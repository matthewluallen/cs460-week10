// Read-only MLX90614 SMBus word decoding. Not an NCIR2 register protocol.
// Melexis DOC#3901090614 rev021: sections 4.1.4.3 and 4.1.8.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace mlx90614 {
constexpr uint8_t Ambient = 0x06, Object = 0x07;
enum class Status : uint8_t { Valid, InvalidRequest, IoError, PecError, SensorError };
struct Word {
    Status status = Status::InvalidRequest;
    uint16_t raw = 0;
};
struct Temperature {
    Status status = Status::InvalidRequest;
    uint16_t raw = 0;
    int32_t centi_c = 0; // Only usable when status == Valid; not calibrated accuracy.
};
// SMBus PEC uses init 0, polynomial 0x07, unlike Sensirion's CRC.
inline uint8_t pec(const uint8_t* data, size_t size) {
    uint8_t crc = 0;
    while (size--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x07) : uint8_t(crc << 1);
    }
    return crc;
}
inline bool readable(uint8_t address, uint8_t command) {
    // No universal address, reserved bus addresses, flags or sleep operations.
    return address >= 8 && address <= 0x77 && command <= 0x3f;
}
inline Word decode_word(uint8_t address, uint8_t command,
                        const uint8_t* bytes, size_t size) {
    if (!readable(address, command) || !bytes || size != 3) return {};
    const uint8_t frame[] = {uint8_t(address << 1), command,
                            uint8_t((address << 1) | 1), bytes[0], bytes[1]};
    if (pec(frame, sizeof(frame)) != bytes[2]) return {Status::PecError, 0};
    return {Status::Valid, uint16_t(uint16_t(bytes[1]) << 8 | bytes[0])};
}
inline Temperature temperature(uint8_t command, const Word& word) {
    if (command != Ambient && command != Object) return {};
    if (word.status != Status::Valid) return {word.status, 0, 0};
    if (word.raw & 0x8000) return {Status::SensorError, word.raw, 0};
    return {Status::Valid, word.raw, int32_t(word.raw) * 2 - 27315};
}
// Transport::reg must send command + repeated START, read 3 bytes, then STOP,
// including abort cleanup. This helper never writes configuration or retries.
// Successful PEC is integrity checking, not authentication, freshness or chip ID.
template<class Transport>
Word read_word(Transport& bus, uint8_t address, uint8_t command) {
    if (!readable(address, command)) return {};
    uint8_t data[3]{};
    if (!bus.reg(address, command, data, sizeof(data))) return {Status::IoError, 0};
    return decode_word(address, command, data, sizeof(data));
}
struct Identity {
    Status status = Status::InvalidRequest;
    uint64_t serial = 0; // Canonical order: EEPROM 0x1C is the most significant word.
};
// Protocol candidate only: PEC and a device-reported ID do not authenticate a SKU.
// EEPROM commands are 0x20 | cell address. No writes to calibration/address cells.
template<class Transport>
Identity read_identity(Transport& bus, uint8_t address) {
    const auto configured = read_word(bus, address, 0x2e);
    if (configured.status != Status::Valid) return {configured.status, 0};
    if (uint8_t(configured.raw) != address) return {};
    uint64_t serial = 0;
    for (uint8_t command = 0x3c; command <= 0x3f; ++command) {
        const auto word = read_word(bus, address, command);
        if (word.status != Status::Valid) return {word.status, 0};
        serial = (serial << 16) | word.raw;
    }
    return {Status::Valid, serial};
}
} // namespace mlx90614
