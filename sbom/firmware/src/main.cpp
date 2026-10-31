#include <Arduino.h>
#include "esp_task_wdt.h"
#include <WiFi.h>
#include "config.h"
#include "m5stamplc_io.h"
#include "modbus_tcp_server.h"
#include "dnp3_outstation.h"
#include "enip_identity.h"
#include "s7_fingerprint.h"
#include "iec104_outstation.h"
#include "opcua_server.h"
#include "bacnet_responder.h"
#include "ota_handler.h"
#include "eth_ota.h"
#include "web_hmi.h"
#include "eth_http.h"
#include "debug_telnet.h"
#include "openplc_runtime.h"
#include "serial_config.h"

// Configuration - set these to your WiFi credentials
const char* WIFI_SSID = "your_ssid";
const char* WIFI_PASS = "your_password";
bool g_net_up = false;

// Timing
unsigned long last_modbus_ms = 0;
unsigned long last_scan_ms = 0;
unsigned long last_display_ms = 0;

// Forward declarations
void setup_wifi();
void print_startup_banner();

#include "plc_engine.h"
#include "io_expansion.h"
#include "plc_core.h"
#include "plc_console.h"
#include "diagnostic_log.h"
#include "acquisition_record.h"
#include "loop_timing.h"
#include "device_inventory.h"
#include "device_acquisition.h"
#include "plc_auth.h"
#include "modbus_serial.h"
#include "modbus_rtu.h"
#include "mqtt_sparkplug.h"
#include "mqtt_transport.h"
#include "network_input.h"
#include "eth_net.h"
#include "time_sync.h"
#include "display_ui.h"
#include "plc_diag.h"
#include "svc_config.h"
#include "port_c.h"
#ifdef ENABLE_GROVE_UNITS
#include "grove_units.h"
#endif
#ifdef ENABLE_PLC_LOADER
#include "program_loader.h"
#endif
#ifdef ENABLE_PLC_VM
plc::Engine g_engine;
plc::Instr  g_prog[32];
int         g_prog_n = 0;
#endif

#ifdef ENABLE_ETHERNET
// Communications processor: a dedicated FreeRTOS task pinned to core 0, running
// independently of the PLC scan on core 1. Real PLCs use a separate comms
// processor so network traffic is never starved by a long logic/I-O scan
// (here the sensor scan is ~35 ms). The W5500 (its own TCP/IP coprocessor) is
// serviced only from this task, so there is no cross-core SPI contention; the
// coil/register image it shares with the scan is guarded by a spinlock.
static void comms_task(void*) {
    uint32_t last_topup = 0, last_hb = 0, beats = 0;
    for (;;) {
        mqtt_w5500_service();     // ESP-MQTT I/O remains on the W5500 owner
        network_input_service(); // native process inputs share this socket owner
        modbus_server.handle();   // Modbus/TCP :502
        eth_http_handle();        // HTTP HMI + JSON API :8080
        time_sync_service();      // NTP client - W5500 I/O stays on the comms core (NTP AiTM target)
        eth_ota_handle();         // W5500 unsigned OTA upload :80 (CWE-494)
        if (svc_enabled(SVC_DNP3)) dnp3_outstation.handle();   // DNP3 outstation :20000
        if (svc_enabled(SVC_ENIP)) enip_identity.handle();     // EtherNet/IP :44818
        if (svc_enabled(SVC_S7))   s7_fingerprint.handle();    // S7/ISO-TSAP :102
        if (svc_enabled(SVC_IEC104)) iec104_outstation.handle(); // IEC-104 :2404
        if (svc_enabled(SVC_OPCUA)) opcua_server.handle();     // OPC-UA :4840
        if (svc_enabled(SVC_BACNET)) bacnet_responder.handle(); // BACnet/IP :47808 (UDP)
        beats++;
        uint32_t now = millis();
        if (now - last_topup >= 250) {
            last_topup = now;
            modbus_server.topup_listeners();
            eth_http_topup();
            eth_ota_topup();
            eth_reap_sockets();   // free half-open / idle W5500 sockets
        }
        if (now - last_hb >= 5000 && !modbus_serial_active()) {
            last_hb = now;
            diagnostic_log_printf(diagnostics::Origin::Telemetry,"[COMMS] alive beats=%lu stackfree=%u listeners=%d\n",
                          (unsigned long)beats, (unsigned)uxTaskGetStackHighWaterMark(NULL), eth_listen_count());
        }
        vTaskDelay(1);   // ~1 kHz service rate, yields to the RTOS
    }
}
#endif

void setup() {
    diagnostic_log_begin(); // ordinary Arduino/IDF task logs never write USB
    Serial.setRxBufferSize(8192);   // full 5500-byte project frame + USB packet headroom
    Serial.begin(115200);
    // Arduino HWCDC decrements an unsigned retry counter when the host stops
    // draining. A zero timeout underflows that counter and can stall until WDT.
    // One millisecond keeps each failed USB write bounded on the physical S3.
    Serial.setTxTimeoutMs(1);
    // NOTE: opening this USB-Serial/JTAG port can reset the CPU (rst:0x15,
    // USB_UART_CHIP_RESET) - the peripheral reacts to host DTR/RTS below the app,
    // so it cannot be suppressed here. For automation prefer the network APIs
    // (HTTP JSON / Modbus / GET /api/reboot). See firmware/docs/CONSOLE.md.
    delay(500);

    print_startup_banner();
    plc_diag_begin();   // latch reset reason; print crash black box on a panic reboot

    serial_config_load();   // load network config (g_cfg) early
    svc_begin();            // load selectable-service enable flags
    port_c_begin();         // establish Port C ownership before any Grove probe
    eth_set_sock_timeout_ms(svc_get_sock_timeout_s() * 1000);  // W5500 socket age-out (NVS)
    plc_auth_begin();       // load user password (plaintext) + device MAC
    // Initialize I/O
    if (!io.init()) {
        Serial.println("ERROR: I/O initialization failed!");
        while (1) delay(1000);
    }

    display_ui_boot();   // LCD boot logo (orientation from M5.begin, not forced)

#ifndef ENABLE_CWE_DEMOS
    // Physical-presence recovery (maintainer escape hatch): holding button A through
    // reset clears the provisioned operator password and returns to first-run
    // enrollment. Covers a forgotten password or an engineer locked out of the box.
    if (display_ui_btnA_held_at_boot()) {
        plc_auth_clear_user_password();
        Serial.println("[AUTH] button A held at boot - operator password cleared; set a new one with 'passwd <new>'");
    }
#endif
    for (int r = 0; r < 4; r++) io.relay_set(r, false);
    Serial.println("[BOOT] outputs OFF; validated project startup policy applies later (no relay pulse test)");
    if (ioexp_begin()) {
        Serial.printf("[IOEXP] StamPLC IO module online at 0x%02X (fw 0x%02X): 2 analog inputs + mechanical relay + 2 SSRs\n", ioexp_addr(), ioexp_fw());
    } else {
        Serial.println("[IOEXP] StamPLC IO module not found (0x20-0x2F); expansion outputs unavailable; no base-relay fallback");
    }
#ifdef ENABLE_GROVE_UNITS
    device_inventory_begin();
    grove::grove.begin();
    Serial.println("[GROVE] units init (ENV on RED; BLUE follows Port C profile)");
#endif
#ifdef ENABLE_ETHERNET
    // --- W5500 PoE coexistence (per M5Stack StamPLC PoE docs) ---
    // The LCD, the PI4IOE IO-expander, and the PoE module share one RST line, and
    // the PoE INT shares the PI4IOE interrupt bus (G14). M5StamPLC.begin() above
    // already released the shared RST. Now mask the PI4IOE interrupt so it stops
    // driving G14 (which corrupts W5500 SPI), then bring Ethernet up with RST
    // left untouched so we never reset the just-initialized display.
    eth_prepare_shared_int();
    eth_begin(g_cfg.eth_ip, g_cfg.eth_gw, g_cfg.eth_mask, g_cfg.eth_use_static);
    time_sync_begin();   // network time client (default source = gateway; auto-sync off)
#endif
    serial_config_menu(5000);
    setup_wifi();
#ifdef ENABLE_ETHERNET
    if (eth_ok()) {
        g_net_up = true;
        Serial.printf("[ETH] network up at %s - starting services\n", eth_ip().c_str());
    }
#endif

    #ifndef OPENPLC_GENERATED_CONTROLLER
    // Initialize OpenPLC runtime
    if (!openplc_runtime.init()) {
        Serial.println("ERROR: OpenPLC runtime init failed!");
        while (1) delay(1000);
    }

    #endif

    if (g_net_up) {
    // Start Modbus TCP server
    if (svc_enabled(SVC_MODBUS_TCP)) {
        if (!modbus_server.start()) { Serial.println("ERROR: Modbus TCP start failed!"); while (1) delay(1000); }
    } else Serial.println("[PROTO] Modbus/TCP disabled by config (proto modbus-tcp on)");

#ifdef ENABLE_ETHERNET
    // HTTP HMI / config editor starts right after Modbus so it reliably claims a
    // W5500 socket before the optional protocols consume the 8-socket pool - the
    // editor is the control plane (protocol config + reboot) and must stay reachable.
    if (svc_enabled(SVC_HTTP_HMI)) eth_http_begin(); else Serial.println("[PROTO] HTTP HMI disabled by config");
#endif

    if (svc_enabled(SVC_DNP3)) dnp3_outstation.start();   // DNP3 outstation :20000 (LS-19)
    if (svc_enabled(SVC_ENIP)) enip_identity.start();     // EtherNet/IP :44818 (LS-20)
    if (svc_enabled(SVC_S7))   s7_fingerprint.start();    // S7/ISO-TSAP :102 (LS-20)
    if (svc_enabled(SVC_IEC104)) iec104_outstation.start(); // IEC-104 :2404 (LS-19)
    if (svc_enabled(SVC_OPCUA)) opcua_server.start();     // OPC-UA :4840 (LS-21)
    if (svc_enabled(SVC_BACNET)) bacnet_responder.start();  // BACnet/IP :47808 (UDP)

#ifdef ENABLE_ETHERNET
    // (HTTP HMI/editor was started earlier, right after Modbus, to claim its socket first.)
    // Selectable fieldbus / IIoT services (LS-18), same I/O image as Modbus/TCP.
    // MQTT dials only a broker someone configured (`mqtt connect <host>` saves it). With
    // none saved nothing is dialed: an unreachable default retried for ever and held a
    // socket, a task and its buffers for nothing.
    if (svc_enabled(SVC_MQTT)) {
        char broker[64]; svc_get_mqtt_broker(broker, sizeof(broker));
        mqtt_sparkplug_begin(broker, MQTT_BROKER_PORT, "TBD", "stamplc");
    }
    eth_ota_begin();  // W5500 unsigned-OTA listener :80 (CWE-494) - twin-visible
#else
    // Start OTA handler (non-fatal)
    if (!ota_handler.start()) Serial.println("WARNING: OTA handler start failed");
    // Start Web HMI (non-fatal)
    if (!web_hmi.start()) Serial.println("WARNING: Web HMI start failed");
    // Start debug telnet (CWE-489: enabled by default)
#if DEBUG_TELNET_ENABLED
    if (!debug_telnet.start()) Serial.println("WARNING: Debug telnet start failed");
#endif
#endif

    }  // g_net_up
    #ifndef OPENPLC_GENERATED_CONTROLLER
    // Start OpenPLC runtime
    // The compatibility image shim does not execute an application.
    // Only a validated project may enter the VM scan path.
#ifdef ENABLE_PLC_VM
    g_engine.reset();
    g_prog_n = 0;
#ifdef ENABLE_PLC_LOADER
    g_prog_n = plc::program_loader.begin(g_prog, 32);   // restore last-good program from NVS
#endif
    Serial.printf("[PLC] project instructions=%d; default PROGRAM; inspect startup policy with project json\n", g_prog_n);
#endif

    #endif

    Serial.println("\n===== Startup complete =====");
    Serial.println("[Main] All services running");
#ifdef ENABLE_CWE_DEMOS
    Serial.println("[WARNING] This firmware contains intentional vulnerabilities for educational purposes");
    Serial.println("[CWE] Embedded CWEs: 798, 319, 306, 494, 78, 489, 330, 22");
#else
    Serial.println("[PROFILE] Secure comparison build: selected remediation branches; see the residual-risk matrix");
#endif
    plc_core_begin();
    acquisition_record_boot_begin(); // immutable before RTU/HTTP workers start
    plc_image_lock(); plc_image_unlock();  // initialize recursive mutex before any worker
    // Local RTU is independent of W5500/Wi-Fi and starts after image/program init.
    // Its dedicated worker owns UART1/DIR, including diagnostic transmissions.
    if (svc_enabled(SVC_MODBUS_RTU)) modbus_rtu_begin(1, 19200);
    plc_console_begin();
#ifdef ENABLE_ETHERNET
    if (g_net_up) {
        xTaskCreatePinnedToCore(comms_task, "plc_comms", 16384, NULL, 6, NULL, 0);
        Serial.println("[NET] comms processor started on core 0 (scan runs on core 1)");
    }
#endif

    // Auto-recovery watchdog: subscribe the loop task now that boot is complete, so a
    // hang in loop() auto-reboots the board and prints a backtrace naming the stuck
    // task, instead of silently wedging until a manual power-cycle.
    int wdt_init,wdt_config;bool wdt_reconfigured=false;
#if ESP_IDF_VERSION_MAJOR >= 5
    { esp_task_wdt_config_t wdt_cfg = { .timeout_ms = execution::WatchdogTimeoutMs, .idle_core_mask = 0, .trigger_panic = true };
      wdt_config=wdt_init=esp_task_wdt_init(&wdt_cfg);
      if (wdt_init == ESP_ERR_INVALID_STATE) {
          wdt_reconfigured=true;wdt_config=esp_task_wdt_reconfigure(&wdt_cfg);
      } }
#else
    wdt_config=wdt_init=esp_task_wdt_init(execution::WatchdogTimeoutMs/1000, true);
#endif
    const int wdt_add=esp_task_wdt_add(NULL),wdt_status=esp_task_wdt_status(NULL);
    plc_execution_watchdog_setup(wdt_init,wdt_reconfigured,wdt_config,wdt_add,wdt_status);
    if (wdt_config==ESP_OK && wdt_status==ESP_OK)
        Serial.println("[WDT] loop task watchdog armed (8s, panic+reboot on hang)");
    else Serial.printf("[WDT] policy not confirmed: config=%d add=%d status=%d; inspect execution json\n",wdt_config,wdt_add,wdt_status);
}

void loop() {
    plc_timing_loop_begin(); // includes WDT, console, acquisition, scan, services and display
    plc_execution_watchdog_feed(esp_task_wdt_reset());
    unsigned long now = millis();
    plc_diag_loop(now);   // 1 Hz black-box checkpoint (cheap, throttled)
    plc_console_poll();   // USB debug console (also receives PLCLOAD program frames)
    diagnostic_log_poll(); // capped, explicitly generated diagnostic exercise
    if (svc_enabled(SVC_MQTT)) mqtt_sparkplug_poll();

    plc_timing_loop_phase(timing::FRONT_SERVICES);
    port_c_poll();          // Port C hall switch (A3144E); no-op unless that profile is active
    device_acquisition_tick();
    // Compatibility telemetry has only the explicit direct ENV role. Routed
    // samples are available through devices samples, never enumeration order.
    grove::Readings env_readings;
    auto& sensors=device_acquisition_state();
    for(size_t i=0;i<sensors.count;++i){const auto& s=sensors.slots[i];
        if(s.kind==acquisition::Kind::Env && !s.device.mux && !s.device.bus){
            env_readings.env_ok=s.current(millis(),sensors.enabled)==acquisition::Quality::Valid;
            env_readings.tempC=s.value.temp_c;env_readings.rh=s.value.rh_pct;
            env_readings.hPa=s.value.pressure_pa/100;env_readings.gas_kohm=s.value.gas_ohm/1000;break;
        }
    }
    grove::grove.set_env(env_readings);
    plc_timing_loop_phase(timing::ACQUISITION);

    // Coordinator eligibility uses current time, not the earlier loop timestamp.
    // Compatibility acquisition can block; anchor the next interval at the actual
    // scan start afterward. Missed intervals do not cause catch-up output bursts.
    if (millis() - last_scan_ms >= OPENPLC_SCAN_CYCLE_MS) {
#ifdef ENABLE_GROVE_UNITS
        grove::grove.update();
#endif
        last_scan_ms = millis();
#ifdef ENABLE_PLC_VM
        plc_core_scan_scheduled(); // separate periodic timing from loader inhibition scans
#endif
    }

    plc_timing_loop_phase(timing::SCAN_SLOT);
    // Service Modbus TCP (lwIP/WiFi path only; the Ethernet path uses the
    // core-0 comms task instead)
#ifndef ENABLE_ETHERNET
    network_input_service();
    modbus_server.handle();
    if (svc_enabled(SVC_DNP3)) dnp3_outstation.handle();
    if (svc_enabled(SVC_ENIP)) enip_identity.handle();
    if (svc_enabled(SVC_S7))   s7_fingerprint.handle();
    if (svc_enabled(SVC_IEC104)) iec104_outstation.handle();
    if (svc_enabled(SVC_OPCUA)) opcua_server.handle();
    if (svc_enabled(SVC_BACNET)) bacnet_responder.handle();
#endif

    // Service OTA uploads
#ifndef ENABLE_ETHERNET
    ota_handler.handle();
#endif

    // Service Web HMI
#ifndef ENABLE_ETHERNET
    web_hmi.handle();
#endif

    // Service debug telnet
#if DEBUG_TELNET_ENABLED
#ifndef ENABLE_ETHERNET
    debug_telnet.handle();
#endif
#endif

    plc_timing_loop_phase(timing::NETWORK_SERVICES);
    // Update display
    if (now - last_display_ms >= 1000 && !modbus_serial_active()) {
        last_display_ms = now;
        static unsigned long last_io = 0;
        if (now - last_io >= 2000) {
            last_io = now;
            if (!console_telemetry_muted()) diagnostic_log_printf(diagnostics::Origin::Telemetry,"[IO] temp=%.1fC Vin=%.2fV I=%.3fA | relays=%d%d%d%d | DI=%d%d%d%d%d%d%d%d\n",
                io.temp_read(), io.voltage_read(), io.current_read(),
                io.relay_get(0), io.relay_get(1), io.relay_get(2), io.relay_get(3),
                io.di_read(0), io.di_read(1), io.di_read(2), io.di_read(3),
                io.di_read(4), io.di_read(5), io.di_read(6), io.di_read(7));
        }
#ifdef ENABLE_GROVE_UNITS
        { const auto& g = grove::grove.last();
          if (!console_telemetry_muted()) diagnostic_log_printf(diagnostics::Origin::Telemetry,"[GROVE] weight_raw=%ld temp=%.1fC rh=%.0f%% p=%.1fhPa (env_ok=%d)\n",
            (long)g.weight_raw, g.tempC, g.rh, g.hPa, g.env_ok?1:0); }
#endif
        // Registers are published every scan by plc_core_scan(); telemetry only here.
        if (!console_telemetry_muted()) diagnostic_log_printf(diagnostics::Origin::Telemetry,"[PLC] mode=%s scan=%luus(max %lu) faults=%d forces=%d expansion_relay=%d | CH1=%umV/%uuA CH2=%umV/%uuA\n",
            plc_mode_str(), (unsigned long)plc_scan_last_us(), (unsigned long)plc_scan_max_us(),
            plc_fault_active_count(), plc_force_count(), plc_coil_live(0)?1:0,
            modbus_server.get_register(10), modbus_server.get_register(11),
            modbus_server.get_register(12), modbus_server.get_register(13));
        io.update_display();

        // Print status to serial every 5 seconds
        static unsigned long last_status = 0;
        if (now - last_status >= 5000) {
            last_status = now;
            if (!console_telemetry_muted()) diagnostic_log_printf(diagnostics::Origin::Telemetry,"[Status] Uptime: %lus | CompletedScans: %llu | WiFi: %d dBm | Heap: %u | LoopStackFree: %u\n",
                         now / 1000,
                         (unsigned long long)plc_timing_snapshot().scan_busy.count,
                         WiFi.RSSI(),
                         esp_get_free_heap_size(),
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }
    }

    plc_timing_loop_phase(timing::TELEMETRY);
    // On-device LCD panel (buttons + render), self-throttled; skip during USB Modbus gateway
    if (!modbus_serial_active()) display_ui_tick();

    // Yield to WiFi stack
    plc_timing_loop_end(); // period to next begin also includes yield/scheduling
    yield();
}

void setup_wifi() {
    g_net_up = false;
    if (g_cfg.wifi_mode == WMODE_OFF) {
        Serial.println("[WiFi] DISABLED by config (contained/serial-only). No radio, no network services.");
        WiFi.mode(WIFI_OFF);
        return;
    }
    if (g_cfg.wifi_mode == WMODE_AP) {
        // Setup/provisioning AP: host a network and serve the web config UI on it, so the
        // device can be configured over WiFi (http://192.168.4.1:8080) as well as USB.
        char apssid[33]; setup_ap_ssid(apssid, sizeof(apssid)); const char* appw = setup_ap_pass();
        Serial.printf("[WiFi] SETUP-AP '%s' (default-cred provisioning - CWE-798/1392 if left unchanged)\n", apssid);
        WiFi.mode(WIFI_AP);
        WiFi.softAP(apssid, appw);
        g_net_up = true;
        Serial.print("[WiFi] AP IP: "); Serial.println(WiFi.softAPIP());
        Serial.printf("[WiFi] Web config: http://%s:8080  (or configure over USB: netcfg)\n", WiFi.softAPIP().toString().c_str());
        return;
    }
    Serial.printf("[WiFi] Connecting to %s...\n", g_cfg.ssid);
    WiFi.mode(WIFI_STA);
    if (g_cfg.wifi_use_static) {
        IPAddress ip, gw, mask;
        ip.fromString(g_cfg.wifi_ip); gw.fromString(g_cfg.wifi_gw); mask.fromString(g_cfg.wifi_mask);
        WiFi.config(ip, gw, mask);
    }
    WiFi.begin(g_cfg.ssid, g_cfg.pass);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(500); Serial.print("."); attempts++; }
    if (WiFi.status() == WL_CONNECTED) {
        g_net_up = true;
        Serial.print("\n[WiFi] Connected! IP: "); Serial.println(WiFi.localIP());
        Serial.printf("[WiFi] Modbus TCP: %s:502\n", WiFi.localIP().toString().c_str());
        Serial.printf("[WiFi] Web HMI: http://%s:8080\n", WiFi.localIP().toString().c_str());
        Serial.printf("[Telnet] Debug shell: telnet %s 23\n", WiFi.localIP().toString().c_str());
    } else {
        if (g_cfg.ap_fallback) {
            Serial.println();
            Serial.println("[WiFi] STA failed. Starting OPEN-AP fallback (VULNERABLE: broadcasts services).");
            WiFi.mode(WIFI_AP);
            WiFi.softAP("M5StampLC-OpenPLC", "openplc123");
            g_net_up = true;
            Serial.print("[WiFi] AP IP: "); Serial.println(WiFi.softAPIP());
        } else {
            Serial.println();
            Serial.println("[WiFi] STA failed. Staying OFFLINE (AP fallback disabled by config - contained).");
            WiFi.mode(WIFI_OFF);
        }
    }
}

void print_startup_banner() {
    Serial.println("\n\n");
    Serial.println("╔═══════════════════════════════════════════╗");
    Serial.println("║  M5 StampLC OpenPLC + Tailscale Lab       ║");
    Serial.println("║  CWE-1358: ICS Security Teaching Platform ║");
    Serial.println("║  Generic project runtime / VM32          ║");
    Serial.println("╚═══════════════════════════════════════════╝");
    Serial.println();
#ifdef ENABLE_CWE_DEMOS
    Serial.println("⚠️  WARNING: This firmware contains");
    Serial.println("   INTENTIONAL VULNERABILITIES for teaching!");
    Serial.println();
    Serial.println("CWE Demonstration:");
    Serial.println("  [CWE-798]  Hardcoded password: admin:openplc");
    Serial.println("  [CWE-319]  Modbus TCP unencrypted on :502");
    Serial.println("  [CWE-306]  Expansion output coil accepts unauthenticated writes");
    Serial.println("  [CWE-494]  OTA accepts unsigned firmware");
    Serial.println("  [CWE-78]   Command injection in diagnostic_ping()");
    Serial.println("  [CWE-489]  Wi-Fi debug telnet on :23 (off in Ethernet profile)");
    Serial.println("  [CWE-330]  Session token predictable (millis-based)");
    Serial.println("  [CWE-22]   Path traversal in log viewer");
    Serial.println("  [CWE-521]  User password capped at 8 chars, A-Z0-9 only");
    Serial.println("  [CWE-256]  User password stored in plaintext (NVS)");
    Serial.println("  [CWE-798]  'service' account password derived from device MAC");
    Serial.println("  [CWE-307]  No lockout on failed logins (brute-forceable)");
    Serial.println();
    // [CWE-200/1273] MAC printed by default - this is the value the service
    // account password is derived from (see 'login service ...').
    Serial.printf("  Device MAC : %s\n", plc_device_mac());
    Serial.println("  Accounts   : operator:<user pw, dflt 1234>  admin:openplc (legacy)");
    Serial.println("               service:<derived from MAC above>  <- backdoor");
    Serial.println("  Console    : 'login <user> <pass>', then 'passwd <new>' / 'svcinfo'");
#else
    Serial.println("SECURE comparison profile - selected remediation branches");
    Serial.println("   ENABLE_CWE_DEMOS off; this label is not a complete-security claim.");
    Serial.println("  [CWE-798/259] operator credential branch changed; provisioning/residual credentials require review");
    Serial.println("  [CWE-306]  Modbus writes refused unless in supervised REMOTE mode");
    Serial.println("  [CWE-494]  OTA requires HMAC signature + authentication");
    Serial.println("  [CWE-78]   diagnostic ping input validated");
    Serial.println("  [CWE-489]  debug telnet disabled");
    Serial.println("  [CWE-330]  session tokens from hardware CSPRNG");
    Serial.println("  [CWE-22]   log-viewer path traversal blocked");
    Serial.println("  [CWE-521]  strong password policy (>=12, mixed classes)");
    Serial.println("  [CWE-256]  passwords stored as salted SHA-256 (not plaintext)");
    Serial.println("  [CWE-307]  login lockout after repeated failures");
    Serial.println("  [CWE-197]  64-bit wall-clock (Y2038-safe)");
    Serial.println("  [CWE-200]  device MAC not leaked in HTTP headers");
    Serial.println();
    Serial.println("  Accounts   : operator:<provisioned, salted-hash>  (no admin/service backdoor)");
    Serial.println("  Console    : 'login operator <pass>' after provisioning");
#endif
    Serial.println();
}
