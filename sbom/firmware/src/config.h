#ifndef CONFIG_H
#define CONFIG_H
#include <Arduino.h>
#include "plc_point_map.h"

// M5 StampLC Hardware Configuration
#define BOARD_M5STAMPLC

// GPIO & I2C Configuration
#define I2C_SDA_PIN 1
#define I2C_SCL_PIN 2
#define I2C_FREQ 100000

// M5 StampLC I/O Expander (MCP23017 / PCA9555)
// Relays: 4x output channels
#define RELAY_EXPANDER_ADDR 0x20
#define RELAY_PIN_0 0
#define RELAY_PIN_1 1
#define RELAY_PIN_2 2
#define RELAY_PIN_3 3

// Digital Inputs: 8x opto-isolated inputs
#define DI_EXPANDER_ADDR 0x21
#define DI_PIN_BASE 0
#define DI_COUNT 8

// Sensors
#define TEMP_SENSOR_ADDR 0x48   // LM75 temperature
#define POWER_MON_ADDR 0x40     // INA226 voltage/current

// Display (LCD)
#define LCD_WIDTH 240
#define LCD_HEIGHT 135
#define LCD_ROTATION 1

// WiFi Configuration
#define WIFI_SSID_MAX 32
#define WIFI_PASS_MAX 64

// Modbus TCP Configuration
#define MODBUS_TCP_PORT 502
// DNP3 (IEEE 1815) outstation - on-device teaching target (LS-19)
#define DNP3_TCP_PORT 20000
#define DNP3_ADDR     4       // outstation link address (master polls addr 4)
// EtherNet/IP + Siemens S7 fingerprint responders - on-device enumeration targets (LS-20)
#define ENIP_TCP_PORT 44818
#define S7_TCP_PORT   102
// IEC 60870-5-104 outstation - on-device teaching target (LS-19)
#define IEC104_TCP_PORT 2404
// OPC-UA discovery responder - on-device teaching target (LS-21)
#define OPCUA_TCP_PORT 4840
#define BACNET_PORT 47808   // BACnet/IP (UDP)

// MQTT / Sparkplug B (LS-18): the student sets the broker with `mqtt connect <host>`
// (saved in NVS, used at the next boot). There is no built-in default broker any more:
// the old 10.13.37.1 placeholder was dialed at every boot and retried for ever.
#define MQTT_BROKER_HOST ""
#define MQTT_BROKER_PORT 1883
#define MODBUS_TCP_MAX_CLIENTS 4
#define MODBUS_SCAN_CYCLE_MS 50

// Modbus register mapping
#define MB_COIL_RELAY_0 (pointmap::RELAY0 + 0)       // Output relay 0
#define MB_COIL_RELAY_1 (pointmap::RELAY0 + 1)       // Output relay 1
#define MB_COIL_RELAY_2 (pointmap::RELAY0 + 2)       // Output relay 2
#define MB_COIL_RELAY_3 (pointmap::RELAY0 + 3)       // Output relay 3
#define MB_COIL_DI_BASE pointmap::DI_MIRROR_BASE     // DI input 0-7 at coil 100-107
#define MB_HOLDING_REG_DI_BASE 200   // DI states as holding registers
#define MB_HOLDING_REG_RELAY_BASE 100 // Relay states as holding registers

// OTA Configuration
#define OTA_HTTP_PORT 80
#define OTA_ENDPOINT "/api/upload"
#define OTA_MAX_SIZE 1327104    // 1.3 MB

// Web HMI Configuration
#define WEB_HMI_PORT 8080
#define WEB_HMI_MAX_CLIENTS 4
#define WEB_SESSION_TIMEOUT_MS 300000  // 5 minutes

// Debug Telnet Configuration
#define DEBUG_TELNET_PORT 23
#define DEBUG_TELNET_ENABLED 1  // CWE-489: Enable debug telnet
#define TELNET_TIMEOUT_MS 60000

// OpenPLC Configuration
#define OPENPLC_SCAN_CYCLE_MS 50
#define OPENPLC_MAX_COILS 512
#define OPENPLC_MAX_REGISTERS 512

// MicroLink Tailscale Configuration
#define MICROLINK_ENABLED 1
#define MICROLINK_DEBUG 0
#define MICROLINK_MTU 1500
// Point to your existing headscale instance
#define TAILSCALE_CONTROL_URL "https://headscale.your-domain.com"
#define TAILSCALE_API_KEY ""  // Set at runtime from config

// Filesystem (SPIFFS/LittleFS)
#define SPIFFS_PARTITION_SIZE 2097152  // 2 MB for logs, configs
#define LOG_DIR "/logs"
#define LOG_MAX_FILES 10
#define LOG_MAX_SIZE 102400    // 100 KB per log file

// ===== INTENTIONAL VULNERABILITIES =====
// These are marked for teaching purposes. Each CWE is documented in lab_guide/

// CWE-798: Hardcoded password
#define MODBUS_HARDCODED_USER "admin"
#define MODBUS_HARDCODED_PASS "openplc"
// CWE-1393: Default password (same as above for demo)

// CWE-319: Cleartext transmission
// Modbus TCP is sent unencrypted over :502
#define MODBUS_CLEARTEXT_ENABLED 1

// CWE-306: Missing authentication for critical function
// E-stop (DI 3 → coil reset) has no permission check
#define ESTOP_REQUIRES_AUTH 0  // CWE-306 teaching point

// CWE-494: Unsigned firmware download
#define OTA_VERIFY_SIGNATURE 0  // CWE-494: No signature check on uploaded .bin
#define OTA_VERIFY_SHA256 0

// CWE-489: Debug code in production
// Telnet debug shell left enabled (CWE-489)
#define PRODUCTION_DEBUG_ENABLED 1

// CWE-330 / CWE-337: Predictable PRNG for session tokens
#define SESSION_TOKEN_PREDICTABLE 1  // Seeds from millis() only
#define SESSION_TOKEN_ENTROPY_BITS 16 // Only 65536 possible values

// CWE-22: Path traversal in file access
// Web HMI log viewer accepts "../" without sanitization
#define LOG_VIEWER_PATH_SANITIZE 0  // CWE-22: No path normalization

// CWE-78: OS Command Injection
// Diagnostic ping function passes unsanitized input to system()
#define DIAGNOSTIC_PING_UNSAFE 1  // CWE-78: Command injection in ping()

#endif // CONFIG_H
