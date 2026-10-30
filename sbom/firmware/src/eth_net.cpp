#include "diagnostic_log.h"
#include "eth_net.h"
#include "serial_config.h"   // g_cfg (adopt leased gateway on DHCP)
#ifdef ENABLE_ETHERNET
// M5Stack-documented approach for the StamPLC PoE (W5500): the Arduino Ethernet
// library drives the W5500 over transaction-arbitrated SPI, so it coexists with
// the LCD on the shared bus (SCK7 MISO9 MOSI8 CS11). The W5500 runs its own
// hardware TCP/IP stack, so the Modbus and web servers use EthernetServer/Client.
#include <SPI.h>
#include <M5Unified.h>
#include <Ethernet_Generic.h>

#define ETH_CS   11
#define ETH_SCK  7
#define ETH_MISO 9
#define ETH_MOSI 8

static bool s_ok = false;
// Mask the PI4IOE IO-expander interrupt(s) so they stop driving the shared INT
// line (G14) that the W5500 PoE module also uses. PI4IOE reg 0x11 = INT_MASK.
// Reading 0x13 (IRQ status) and 0x01 (chip id) clears any pending latch. We hit
// every PI4IOE that answers on the internal bus (base 0x43, PLC-input 0x44).
void eth_prepare_shared_int() {
    const uint8_t PI4IO_INT_MASK = 0x11, PI4IO_IRQ_STA = 0x13, PI4IO_CHIP_ID = 0x01;
    for (uint8_t addr : {0x43, 0x44, 0x45}) {
        if (!m5::In_I2C.writeRegister8(addr, PI4IO_INT_MASK, 0xFF, 400000)) continue;
        (void)m5::In_I2C.readRegister8(addr, PI4IO_IRQ_STA, 400000);
        (void)m5::In_I2C.readRegister8(addr, PI4IO_CHIP_ID, 400000);
        Serial.printf("[ETH] masked PI4IOE INT at 0x%02X (shared G14)\n", addr);
    }
}


bool eth_begin(const char* ip, const char* gw, const char* mask, bool use_static) {
    static byte mac[6] = {0x02, 0x00, 0x00, 0x13, 0x37, 0x10};   // locally administered
    Serial.println("[ETH] W5500 via Arduino Ethernet library (SPI2 SCK7 MISO9 MOSI8 CS11)...");
    SPI.begin(ETH_SCK, ETH_MISO, ETH_MOSI, -1);
    Ethernet.init(ETH_CS);
    IPAddress lip, g, m, dns;
    lip.fromString(ip); g.fromString(gw); m.fromString(mask); dns.fromString(gw);
    if (use_static) {
        Ethernet.begin(mac, lip, dns, g, m);   // static IP (non-blocking with static)
    } else {
        // DHCP: request a lease (blocking, bounded). On an isolated lab segment the
        // DHCP server is trusted infrastructure - a ROGUE one can hand out its own
        // address as gateway/DNS/NTP (option 42), which is how an AiTM gets on the
        // time path. In the contained twin there is no DHCP server, so fall back to
        // the configured static address rather than boot with no network.
        Serial.println("[ETH] DHCP: requesting a lease (10s timeout)...");
        if (Ethernet.begin(mac, 10000, 4000) == 0) {
            Serial.println("[ETH] DHCP failed - falling back to configured static IP");
            Ethernet.begin(mac, lip, dns, g, m);
        } else {
            // Adopt the leased gateway as the (in-memory) default: an NTP source of
            // "gateway" then follows whatever DHCP handed out - so a rogue DHCP server
            // that sets itself as the gateway also becomes the device's time source.
            Ethernet.gatewayIP().toString().toCharArray(g_cfg.eth_gw, sizeof(g_cfg.eth_gw));
            Serial.printf("[ETH] DHCP lease: ip=%s gw=%s\n",
                          Ethernet.localIP().toString().c_str(), g_cfg.eth_gw);
        }
    }
    delay(200);
    int hw = (int)Ethernet.hardwareStatus();
    int lk = (int)Ethernet.linkStatus();   // 0 Unknown, 1 LinkON, 2 LinkOFF
    s_ok = (hw != 0);                       // EthernetNoHardware == 0
    Serial.printf("[ETH] hardware=%d link=%d ip=%s\n", hw, lk, Ethernet.localIP().toString().c_str());
    return s_ok;
}
bool   eth_up() { return Ethernet.linkStatus() == LinkON; }
bool   eth_ok() { return s_ok; }
String eth_ip() { return Ethernet.localIP().toString(); }

int eth_listen_count(uint16_t port) {
    W5100.beginSPITransaction();
    int n = 0;
    for (uint8_t i = 0; i < 8; i++)
        if (W5100.readSnSR(i) == 0x14 && (!port || W5100.readSnPORT(i) == port)) n++;
    W5100.endSPITransaction();
    return n;
}

void eth_dump_sockets() {
    W5100.beginSPITransaction();
    uint8_t st[8]; uint16_t pt[8];
    for (uint8_t i = 0; i < 8; i++) { st[i] = W5100.readSnSR(i); pt[i] = W5100.readSnPORT(i); }
    W5100.endSPITransaction();
    Serial.print("[ETH] sockets:");
    for (uint8_t i = 0; i < 8; i++) Serial.printf(" %d=%02x/%u", i, st[i], pt[i]);
    Serial.println();
}

// ---- W5500 socket age-out reaper -----------------------------------------
// The W5500 has only 8 hardware TCP sockets, shared by every service (Modbus,
// HTTP, DNP3, S7, OPC-UA, IEC-104, EtherNet/IP). A peer that vanishes without a
// clean FIN leaves its socket stuck (CLOSE_WAIT half-open, or ESTABLISHED but
// idle), and a burst of rapid connects can strand sockets in transient states.
// Each stuck socket permanently shrinks the pool until new connections - and
// re-arming the HTTP/Modbus listeners (topup) - start getting refused. This
// reaper walks all 8 sockets and force-closes any unproductive longer than a
// configurable age-out, returning them to CLOSED for reuse. It runs inside the
// single comms task, between the server handle() calls, so there is no race
// with a socket a server is actively servicing.
static uint32_t s_sock_since[8]  = {0};   // millis when socket entered its current state
static uint32_t s_sock_rxmark[8] = {0};   // last RX byte count seen (activity marker)
static uint8_t  s_sock_state[8]  = {0};   // last SnSR seen (0x00 = CLOSED at boot)
static uint32_t s_sock_timeout_ms = 30000;   // default 30 s; 0 disables; set from NVS at boot
static uint32_t s_reaped = 0;

void     eth_set_sock_timeout_ms(uint32_t ms) { s_sock_timeout_ms = ms; }
uint32_t eth_get_sock_timeout_ms() { return s_sock_timeout_ms; }
uint32_t eth_reaped_count() { return s_reaped; }

void eth_reap_sockets() {
    if (s_sock_timeout_ms == 0) return;                 // reaper disabled
    uint32_t now = millis();
    uint8_t  st[8]; uint16_t rx[8];
    W5100.beginSPITransaction();
    for (uint8_t s = 0; s < 8; s++) { st[s] = W5100.readSnSR(s); rx[s] = W5100.readSnRX_RSR(s); }
    W5100.endSPITransaction();
    for (uint8_t s = 0; s < 8; s++) {
        // A state change or newly-arrived RX is activity: restart the clock.
        if (st[s] != s_sock_state[s] || rx[s] != s_sock_rxmark[s]) {
            s_sock_state[s] = st[s]; s_sock_rxmark[s] = rx[s]; s_sock_since[s] = now;
        }
        // Only TCP connection states are reapable. Skip LISTEN, CLOSED, a UDP
        // socket (SOCK_UDP 0x22, e.g. the BACnet responder), and TCP setup states -
        // reaping a UDP socket would silently kill BACnet.
        bool half_open = (st[s] == SnSR::CLOSE_WAIT);   // 0x1C: peer sent FIN, we have not closed
        bool tcp_conn  = half_open || st[s] == SnSR::ESTABLISHED   // 0x17
                         || st[s] == 0x18 || st[s] == 0x1A         // FIN_WAIT / CLOSING
                         || st[s] == 0x1B || st[s] == 0x1D;        // TIME_WAIT / LAST_ACK
        if (!tcp_conn) continue;
        if (half_open && rx[s] > 0) continue;           // still has buffered data to be read
        // Half-open sockets are dead weight - reap them quickly (a fraction of the
        // age-out). Everything else (idle ESTABLISHED, stuck SYN_RECV/FIN/TIME_WAIT)
        // gets the full configured window.
        uint32_t limit = half_open ? (s_sock_timeout_ms / 8 + 200) : s_sock_timeout_ms;
        if (now - s_sock_since[s] >= limit) {
            W5100.beginSPITransaction();
            W5100.execCmdSn(s, Sock_CLOSE);
            W5100.endSPITransaction();
            diagnostic_log_printf(diagnostics::Origin::Ethernet,"[ETH] reaped socket %u (state 0x%02x, idle %lums) -> pool\n",
                          s, st[s], (unsigned long)(now - s_sock_since[s]));
            s_sock_state[s] = SnSR::CLOSED; s_sock_rxmark[s] = 0; s_sock_since[s] = now;
            s_reaped++;
        }
    }
}

#else
bool   eth_begin(const char*, const char*, const char*, bool) { return false; }
bool   eth_up() { return false; }
bool   eth_ok() { return false; }
String eth_ip() { return String(); }
void   eth_dump_sockets() {}
int    eth_listen_count(uint16_t) { return 0; }
void     eth_reap_sockets() {}
void     eth_set_sock_timeout_ms(uint32_t) {}
uint32_t eth_get_sock_timeout_ms() { return 0; }
uint32_t eth_reaped_count() { return 0; }
#endif
