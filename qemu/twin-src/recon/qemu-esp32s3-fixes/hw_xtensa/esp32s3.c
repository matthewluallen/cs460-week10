/*
 * ESP32S3 SoC and Machine
 *
 * Copyright (c) 2023-2024 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/i2c/tbd_bridge.h"
#include "hw/i2c/i2c_mux_pca954x.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/memalign.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/xtensa/xtensa_memory.h"
#include "hw/misc/unimp.h"
#include "hw/irq.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties.h"
#include "qemu/sockets.h"
#include "qemu/main-loop.h"

/* TBD twin: internal process-physics loop (defined in hw/i2c/qemu_i2c_glue.c).
 * Reads the AW9523 relay outputs and drives the INA226/LM75B sensor registers. */


#include "qemu/osdep.h"
#include "hw/hw.h"
#include "target/xtensa/cpu.h"

#include "hw/misc/esp32s3_rtc_cntl.h"
#include "hw/xtensa/esp32s3_intc.h"

#include "hw/sd/dwc_sdmmc.h"
#include "hw/misc/ssi_psram.h"
#include "core-esp32s3/core-isa.h"
#include "qemu/datadir.h"
#include "sysemu/sysemu.h"
#include "sysemu/reset.h"
#include "sysemu/cpus.h"
#include "sysemu/runstate.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-backend.h"
#include "exec/exec-all.h"
#include "net/net.h"
#include "elf.h"

#include "hw/ssi/esp32s3_spi.h"
#include "hw/misc/esp32s3_cache.h"
#include "hw/char/esp32s3_uart.h"
#include "hw/misc/esp32s3_rng.h"

#include "hw/nvram/esp32s3_efuse.h"
#include "hw/xtensa/esp32s3_clk.h"
#include "hw/dma/esp32s3_gdma.h"
#include "hw/misc/esp32s3_sha.h"
#include "hw/misc/esp32s3_aes.h"
#include "hw/misc/esp32s3_rsa.h"
#include "hw/misc/esp32s3_hmac.h"
#include "hw/misc/esp32s3_ds.h"
#include "hw/timer/esp32s3_timg.h"
#include "hw/timer/esp32s3_systimer.h"
#include "hw/gpio/esp32s3_gpio.h"
#include "hw/misc/esp32s3_xts_aes.h"
#include "hw/misc/esp32s3_pms.h"
#include "hw/net/can/esp32s3_twai.h"
#include "net/can_emu.h"            /* TBD twin: passive CAN bus monitor client */
#include "hw/i2c/esp32_i2c.h"       /* TBD twin: I2C controller for the S3 machine */

/* TBD twin: Grove HX711 load-cell bit-bang model (defined in hw/gpio/esp32_gpio.c);
 * declared here now that Esp32GpioState is in scope via esp32s3_gpio.h above. */


#include "cpu_esp32s3.h"

#include "hw/misc/esp32c3_jtag.h"
#include "hw/display/esp_rgb.h"

#define TYPE_ESP32S3_SOC "xtensa.esp32s3"
#define ESP32S3_SOC(obj) OBJECT_CHECK(Esp32s3SocState, (obj), TYPE_ESP32S3_SOC)

#define TYPE_ESP32S3_CPU XTENSA_CPU_TYPE_NAME("esp32s3")


enum {
    ESP32S3_MEMREGION_IROM,
    ESP32S3_MEMREGION_DROM,
    ESP32S3_MEMREGION_DRAM,
    ESP32S3_MEMREGION_IRAM,
    ESP32S3_MEMREGION_ICACHE,
    ESP32S3_MEMREGION_DCACHE,
    ESP32S3_MEMREGION_RTCSLOW,
    ESP32S3_MEMREGION_RTCFAST,
    ESP32S3_MEMREGION_FRAMEBUF,
};

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} esp32s3_memmap[] = {
    [ESP32S3_MEMREGION_DROM] = { 0x3ff00000, 0x20000 },
    [ESP32S3_MEMREGION_IROM] = { 0x40000000, 0x60000 },
    [ESP32S3_MEMREGION_DRAM] = { 0x3FC80000, 0x170000 },
    [ESP32S3_MEMREGION_IRAM] = { 0x40370000, 0x80000 },
    [ESP32S3_MEMREGION_DCACHE] = { 0x3c000000, ESP32S3_EXTMEM_REGION_SIZE },
    [ESP32S3_MEMREGION_ICACHE] = { 0x42000000, ESP32S3_EXTMEM_REGION_SIZE },
    [ESP32S3_MEMREGION_RTCSLOW] = { 0x50000000, 0x2000 },
    [ESP32S3_MEMREGION_RTCFAST] = { 0x600fe000, 0x2000 },
    /* Virtual Framebuffer, used for the graphical interface */
    [ESP32S3_MEMREGION_FRAMEBUF] = { 0x20000000, ESP_RGB_MAX_VRAM_SIZE },
};


#define ESP32S3_SOC_RESET_PROCPU    0x1
#define ESP32S3_SOC_RESET_APPCPU    0x2
#define ESP32S3_SOC_RESET_PERIPH    0x4
#define ESP32S3_SOC_RESET_DIG       (ESP32S3_SOC_RESET_PROCPU | ESP32S3_SOC_RESET_APPCPU | ESP32S3_SOC_RESET_PERIPH)
#define ESP32S3_SOC_RESET_RTC       0x8
#define ESP32S3_SOC_RESET_ALL       (ESP32S3_SOC_RESET_RTC | ESP32S3_SOC_RESET_DIG)

#define ESP32S3_IO_WARNING  0

typedef struct Esp32s3SocState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    XtensaCPU cpu[ESP32S3_CPU_COUNT];
    Esp32s3IntMatrixState intmatrix;
    ESP32S3UARTState uart[ESP32S3_UART_COUNT];
    ESP32S3GPIOState gpio;
    Esp32s3RngState rng;
    Esp32S3TWAIState twai;
    Esp32I2CState i2c[2];          /* TBD twin: I2C0/I2C1 controllers */

    Esp32s3RtcCntlState rtc_cntl;

    BusState rtc_bus;
    BusState periph_bus;

    MemoryRegion cpu_specific_mem[ESP32S3_CPU_COUNT];
    ESP32S3SpiState spi1;
    ESP32S3CacheState cache;
    ESP32S3EfuseState efuse;
    ESP32S3ClockState clock;
    ESP32S3GdmaState gdma;
    ESP32S3ShaState sha;
    ESP32S3AesState aes;
    ESP32S3RsaState rsa;
    ESP32S3HmacState hmac;
    ESP32S3DsState ds;
    ESP32S3PmsState pms;

    ESP32S3XtsAesState xts_aes;
    ESP32S3TimgState timg[2];
    ESP32S3SysTimerState systimer;

    ESP32C3UsbJtagState jtag;
    ESPRgbState rgb;

    MemoryRegion iomem;
    DWCSDMMCState sdmmc;
    DeviceState *eth;
    SsiPsramState *psram;

    uint32_t requested_reset;
} Esp32s3SocState;


/* Temporary macro to mark the CPU as in non-debugging mode */
#define A_ASSIST_DEBUG_CORE_0_DEBUG_MODE_REG    0x098

/* "QEMU" as a 32-bit value, can be used by the application to to check whether it is running in
 * QEMU or on real hardware */
#define RGB_QEMU_ORIGIN     0x51454d55
#define RGB_QEMU_ORIGIN_REG 0x3F8

static void remove_cpu_watchpoints(XtensaCPU* xcs)
{
    for (int i = 0; i < MAX_NDBREAK; ++i) {
        if (xcs->env.cpu_watchpoint[i]) {
            cpu_watchpoint_remove_by_ref(CPU(xcs), xcs->env.cpu_watchpoint[i]);
            xcs->env.cpu_watchpoint[i] = NULL;
        }
    }
}

static void esp32s3_dig_reset(void *opaque, int n, int level)
{
    Esp32s3SocState *s = ESP32S3_SOC(opaque);
    if (level) {
        s->requested_reset = ESP32S3_SOC_RESET_DIG;
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static void esp32s3_cpu_reset(void* opaque, int n, int level)
{
    Esp32s3SocState *s = ESP32S3_SOC(opaque);
    if (level) {
        s->requested_reset = (n == 0) ? ESP32S3_SOC_RESET_PROCPU : ESP32S3_SOC_RESET_APPCPU;
        /* Use different cause for APP CPU so that its reset doesn't cause QEMU to exit,
         * when -no-reboot option is given.
         */
        ShutdownCause cause = (n == 0) ? SHUTDOWN_CAUSE_GUEST_RESET : SHUTDOWN_CAUSE_SUBSYSTEM_RESET;
        qemu_system_reset_request(cause);
    }
}

static void esp32s3_soc_reset(DeviceState *dev)
{
    Esp32s3SocState *s = ESP32S3_SOC(dev);
    if (s->requested_reset == 0) {
        s->requested_reset = ESP32S3_SOC_RESET_ALL;
    }
    if (s->requested_reset & ESP32S3_SOC_RESET_PERIPH) {
        device_cold_reset(DEVICE(&s->intmatrix));
        for (int i = 0; i < ESP32S3_UART_COUNT; ++i) {
            device_cold_reset(DEVICE(&s->uart[i]));
        }
    }
    if (s->requested_reset & ESP32S3_SOC_RESET_PROCPU) {
        xtensa_select_static_vectors(&s->cpu[0].env, s->rtc_cntl.stat_vector_sel[0]);
        remove_cpu_watchpoints(&s->cpu[0]);
        cpu_reset(CPU(&s->cpu[0]));
    }
    if (s->requested_reset & ESP32S3_SOC_RESET_APPCPU && (ESP32S3_CPU_COUNT > 1)) {
        xtensa_select_static_vectors(&s->cpu[1].env, s->rtc_cntl.stat_vector_sel[1]);
        remove_cpu_watchpoints(&s->cpu[1]);
        cpu_reset(CPU(&s->cpu[1]));
    }
    s->requested_reset = 0;
}

static void esp32s3_cpu_stall(void* opaque, int n, int level)
{
}

static void esp32s3_clk_update(void* opaque, int n, int level)
{
    if (!level) {
        return;
    }
}

static void esp32s3_soc_add_periph_device(MemoryRegion *dest, void* dev, hwaddr dport_base_addr)
{
    MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion_overlap(dest, dport_base_addr, mr, 0);
    MemoryRegion *mr_apb = g_new(MemoryRegion, 1);
    char *name = g_strdup_printf("mr-apb-0x%08x", (uint32_t) dport_base_addr);
    memory_region_init_alias(mr_apb, OBJECT(dev), name, mr, 0, memory_region_size(mr));
    g_free(name);
}

#define MB (1024*1024)

static void esp32s3_init_spi_flash(Esp32s3SocState *ms, BlockBackend* blk)
{
    DeviceState *spi_master = DEVICE(&ms->spi1);
    BusState* spi_bus = qdev_get_child_bus(spi_master, "spi");
    const char* flash_model = NULL;
    int64_t image_size = blk_getlength(blk);

    switch (image_size) {
        case 2 * MB:
            flash_model = "w25x16";
            break;
        case 4 * MB:
            flash_model = "gd25q32";
            break;
        case 8 * MB:
            flash_model = "gd25q64";
            break;
        case 16 * MB:
            flash_model = "is25lp128";
            break;
        default:
            error_report("Drive size error: only 2, 4, 8, and 16MB images are supported");
            return;
    }

    /* Create the SPI flash model */
    DeviceState *flash_dev = qdev_new(flash_model);
    qdev_prop_set_drive(flash_dev, "drive", blk);

    /* Realize the SPI flash, its "drive" (blk) property must already be set! */
    qdev_realize(flash_dev, spi_bus, &error_fatal);
    qdev_connect_gpio_out_named(spi_master, SSI_GPIO_CS, 0,
                                qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0));
}

static void esp32s3_machine_init_psram(Esp32s3SocState *ms, uint32_t size_mbytes)
{
    /* PSRAM attached to SPI1, CS1 */
    DeviceState *spi_master = DEVICE(&ms->spi1);
    BusState* spi_bus = qdev_get_child_bus(spi_master, "spi");
    DeviceState *psram = qdev_new(TYPE_SSI_PSRAM);
    qdev_prop_set_uint32(psram, "size_mbytes", size_mbytes);
    qdev_prop_set_uint8(psram, "cs", 1);
    qdev_realize(psram, spi_bus, &error_fatal);
    ms->psram = SSI_PSRAM(psram);
    qdev_connect_gpio_out_named(spi_master, SSI_GPIO_CS, 1,
                                qdev_get_gpio_in_named(psram, SSI_GPIO_CS, 0));
}

static void esp32s3_machine_init_sd(Esp32s3SocState* ss)
{
    DriveInfo *dinfo = drive_get(IF_SD, 0, 0);
    if (dinfo) {
        DeviceState *card;

        card = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        /* See the comment on not using sysbus-default in esp32_machine_init_i2c */
        DeviceState *sdmmc = DEVICE(&ss->sdmmc);
        SDBus* sd_bus = SD_BUS(qdev_get_child_bus(sdmmc, "sd-bus"));
        qdev_realize_and_unref(card, BUS(sd_bus), &error_fatal);
    }
}

struct Esp32s3MachineState {
    MachineState parent;

    Esp32s3SocState esp32s3;
    DeviceState *flash_dev;
};
#define TYPE_ESP32S3_MACHINE MACHINE_TYPE_NAME("esp32s3")

static void esp32s3_init_openeth(Esp32s3SocState *ms)
{
    MemoryRegion* mr = NULL;
    SysBusDevice* sbd = NULL;

    MemoryRegion* sys_mem = get_system_memory();

    /* Create a new OpenCores Ethernet component */
    DeviceState* open_eth_dev = qemu_create_nic_device("open_eth", true, NULL);
    if (!open_eth_dev) {
        return;
    }
    ms->eth = open_eth_dev;
    sbd = SYS_BUS_DEVICE(open_eth_dev);
    sysbus_realize(sbd, &error_fatal);

    /* OpenCores Ethernet has two memory regions: one for registers and one for descriptors,
        * we need to provide one I/O range for each of them */
    mr = sysbus_mmio_get_region(sbd, 0);
    memory_region_add_subregion_overlap(sys_mem, DR_REG_EMAC_BASE, mr, 0);
    mr = sysbus_mmio_get_region(sbd, 1);
    memory_region_add_subregion_overlap(sys_mem, DR_REG_EMAC_BASE + 0x400, mr, 0);

    sysbus_connect_irq(sbd, 0,
                        qdev_get_gpio_in(DEVICE(&ms->intmatrix), ETS_ETH_MAC_INTR_SOURCE));
}


/* TBD twin: minimal SAR-ADC done stub. M5Unified's init runs ESP-IDF ADC
 * calibration, which starts a conversion in the SENS/RTC ADC domain and
 * busy-waits on the conversion-done bit (0x6000880C bit 16). QEMU models no ADC,
 * so the bit never sets and boot hangs before I2C. We return done + a mid-scale
 * result for the status/result register (offset 0x0C) and 0 for config offsets.
 * Twin sensor values come from the I2C models/physics, not the internal ADC, so
 * a coarse ADC readback here is harmless. */
static uint32_t tbd_adc_regs[0x1000 / 4];   /* config writes persist here */
static uint64_t tbd_saradc_read(void *opaque, hwaddr addr, unsigned size)
{
    uint32_t off = (uint32_t)(addr & 0xFFCU);
    uint32_t v = tbd_adc_regs[off >> 2];
    /* Force the SAR-ADC "measurement done" bit (bit 16) on the registers the
     * ESP-IDF ADC calibration polls: SENS_SAR_MEAS1_CTRL2 (0x30) and the ADC
     * result/status word (0x0C). Add a mid-scale result so cal math stays sane. */
    if (off == 0x0C || off == 0x30) {
        v |= (1u << 16);
        if ((v & 0xFFFFU) == 0) {
            v |= 0x0800u;
        }
    }
    return v;
}
static void tbd_saradc_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    tbd_adc_regs[(addr & 0xFFCU) >> 2] = (uint32_t)val;
}
static const MemoryRegionOps tbd_saradc_ops = {
    .read = tbd_saradc_read,
    .write = tbd_saradc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* TBD twin: minimal GP-SPI (SPI2/SPI3) stub so M5GFX board auto-detection
 * succeeds. M5GFX reads the LCD panel ID (ST7789 RDDID) over the GP-SPI host and
 * accepts the board only if (id & 0xFB) == 0x81 (i.e. 0x85). The S3 machine
 * doesn't model GP-SPI, so the read of the W0..W15 data registers (0x98..0xD4)
 * returns 0 → detection fails → M5Unified registers no IO-expander → null-deref.
 * Returning 0x85 in the data words makes detection pass; 0 elsewhere so the
 * transaction-busy poll (SPI_CMD USR bit) reads 'done' and nothing hangs. */
static uint64_t tbd_spi_id_read(void *opaque, hwaddr addr, unsigned size)
{
    uint32_t off = (uint32_t)(addr & 0xFFCU);
    if (off >= 0x98 && off <= 0xD4) {
        /* M5GFX probes several ST7789 boards in order; they share this SPI host,
         * so answer with the ST7789 ID ONLY when the StamPLC's unique LCD chip-
         * select (GPIO12) is actively driven low — the StamPLC probe's signature.
         * Other boards (CS on GPIO5/7/…) then read 0 and fail, so StamPLC wins. */
        uint32_t out = 0, en = 0;
        cpu_physical_memory_read(0x60004004, &out, 4);   /* GPIO_OUT_REG   */
        cpu_physical_memory_read(0x60004020, &en, 4);    /* GPIO_ENABLE_REG */
        if ((en & (1u << 12)) && !(out & (1u << 12))) {
            return 0x00000085u;   /* ST7789 panel ID → StamPLC detected */
        }
        return 0;                 /* not StamPLC's probe → fails id check */
    }
    return 0;
}
static void tbd_spi_id_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}
static const MemoryRegionOps tbd_spi_id_ops = {
    .read = tbd_spi_id_read,
    .write = tbd_spi_id_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* ==== TBD twin: instrumented FSPI (SPI2) capture harness for the W5500 ====
 * The StamPLC PoE module's W5500 hangs off SPI2 (FSPI): SCK7 MISO9 MOSI8, and a
 * software chip-select on GPIO11 (the LCD is CS=GPIO12, SD=GPIO10 on the same
 * bus). The Arduino Ethernet_Generic library drives the W5500 over this SPI.
 * This handler RAM-backs the FSPI register file, auto-clears the transaction
 * busy bit so the driver's poll completes, preserves the LCD panel-ID answer,
 * and LOGS every access so we can read the exact W5500 command frames the driver
 * emits (VERSIONR probe, PHYCFGR, per-socket setup) before writing the model. */
#define FSPI_REGFILE_WORDS  (0x1000 / 4)
static uint32_t s_fspi_reg[FSPI_REGFILE_WORDS];
static int s_w5dbg = 0;   /* set from TBD_W5DBG env */

/* FSPI (GP-SPI2) register offsets we care about (ESP32-S3 TRM). */
#define A_SPI_CMD        0x00   /* bit24 = SPI_USR (start; HW clears when done) */
#define A_SPI_ADDR       0x04
#define A_SPI_MS_DLEN    0x1C   /* SPI_MS_DLEN_REG: bit-length-1 of the xfer     */
#define A_SPI_MISO_DLEN  0x28
#define A_SPI_MOSI_DLEN  0x24
#define A_SPI_W0         0x98
#define A_SPI_W15        0xD4
#define SPI_USR_BIT      (1u << 24)

/* which shared-bus device is currently chip-selected (active-low GPIO)? */
static const char *tbd_fspi_active_cs(void)
{
    uint32_t out = 0, en = 0;
    cpu_physical_memory_read(0x60004004, &out, 4);   /* GPIO_OUT_REG    */
    cpu_physical_memory_read(0x60004020, &en, 4);    /* GPIO_ENABLE_REG */
    if ((en & (1u << 11)) && !(out & (1u << 11))) return "W5500";  /* CS=G11 */
    if ((en & (1u << 12)) && !(out & (1u << 12))) return "LCD";    /* CS=G12 */
    if ((en & (1u << 10)) && !(out & (1u << 10))) return "SD";     /* CS=G10 */
    return "-";
}

/* ========================= TBD twin: W5500 model =========================
 * A register-level model of the WIZnet W5500 (StamPLC PoE module) on the FSPI
 * bus. The Ethernet_Generic library drives it in VDM: each CS-low session is
 * [addr_hi][addr_lo][control][data...], control = (BSB<<3)|(RWB<<2)|OM. The
 * chip runs its own TCP/IP stack in 8 hardware sockets; we bridge each socket
 * to a host socket (added in the socket milestone). For now: common + socket
 * register files, buffers, and correct SPI framing so the driver detects the
 * chip (VERSIONR=0x04, PHYCFGR link-up) and brings the interface up. */
#define W5_NSOCK      8
#define W5_BUFSZ      2048            /* 2KB per socket TX/RX (default split)   */
typedef struct {
    uint8_t  common[0x100];
    uint8_t  sreg[W5_NSOCK][0x100];
    uint8_t  txbuf[W5_NSOCK][W5_BUFSZ];
    uint8_t  rxbuf[W5_NSOCK][W5_BUFSZ];
    /* per-CS-session frame state machine */
    int      phase;                   /* 0 ah,1 al,2 ctl,3 data                 */
    uint16_t addr;
    uint8_t  ctrl;
    int      bsb, sock, region, rwb;  /* decoded from ctrl                      */
    uint16_t data_off;                /* running offset (VDM auto-increment)    */
    int      initialized;
} TbdW5500;
static TbdW5500 s_w5;
static int s_w5_trace = 0, s_w5_tracen = 0;
static int s_w5_conn[8];                          /* connected host fd, -1 none  */
typedef struct { uint16_t port; int fd; } W5Listener;
static W5Listener s_w5_lsn[8];                     /* one host listener per port  */

static void tbd_w5500_init_regs(void)
{
    if (s_w5.initialized) return;
    memset(&s_w5, 0, sizeof(s_w5));
    s_w5.common[0x39] = 0x04;         /* VERSIONR: W5500 identifies as 0x04      */
    /* PHYCFGR (0x2E): bit0 LNK=1 (link up), bit1 SPD=1 (100M), bit2 DPX=1 (full),
     * bit7 opmode-config-by-reg. 0x87 reads as "link up, 100BASE-TX full". */
    s_w5.common[0x2E] = 0x87;
    s_w5.phase = 0;
    for (int i = 0; i < W5_NSOCK; i++) { s_w5_conn[i] = -1; s_w5_lsn[i].fd = -1; }
    s_w5.initialized = 1;
}

/* ---- W5500 socket <-> host TCP bridge -------------------------------------
 * The W5500 runs its own TCP/IP stack in 8 hardware sockets. We give each
 * socket a real host TCP socket: a Modbus/HTTP LISTEN on the firmware's port
 * becomes a host listener on 127.0.0.1:<port>, an inbound connection lands in
 * the socket's RX buffer, and SEND flushes its TX buffer to the peer. Both the
 * MMIO path (vCPU) and the fd callbacks (main loop) run under the BQL, so the
 * shared register/buffer state needs no extra locking. */
#define SN_MR 0x00
#define SN_CR 0x01
#define SN_IR 0x02
#define SN_SR 0x03
#define SN_PORT 0x04
#define SN_TX_FSR 0x20
#define SN_TX_RD  0x22
#define SN_TX_WR  0x24
#define SN_RX_RSR 0x26
#define SN_RX_RD  0x28
#define SN_RX_WR  0x2A
#define SS_CLOSED 0x00
#define SS_INIT   0x13
#define SS_LISTEN 0x14
#define SS_ESTAB  0x17
#define SS_CLOSE_WAIT 0x1C
#define SS_UDP    0x22

static uint16_t sn_g16(int s, int off){ return (s_w5.sreg[s][off]<<8)|s_w5.sreg[s][off+1]; }
static void sn_s16(int s, int off, uint16_t v){ s_w5.sreg[s][off]=v>>8; s_w5.sreg[s][off+1]=v&0xff; }

static void w5_recv_from_host(int s)
{
    if (s_w5_conn[s] < 0) return;
    uint16_t wr = sn_g16(s, SN_RX_WR), rd = sn_g16(s, SN_RX_RD);
    int used = (uint16_t)(wr - rd);
    int space = W5_BUFSZ - used;
    if (space <= 0) return;
    uint8_t tmp[W5_BUFSZ];
    int n = recv(s_w5_conn[s], (char *)tmp, space, 0);
    if (n > 0) {
        for (int i = 0; i < n; i++) s_w5.rxbuf[s][(uint16_t)(wr + i) & (W5_BUFSZ - 1)] = tmp[i];
        wr += n;
        sn_s16(s, SN_RX_WR, wr);
        sn_s16(s, SN_RX_RSR, (uint16_t)(wr - rd));
        s_w5.sreg[s][SN_IR] |= 0x04;             /* RECV */
        if (s_w5dbg) fprintf(stderr, "W5.sock%d RX +%d (rsr=%u)\n", s, n, (uint16_t)(wr - rd));
    } else if (n == 0) {                         /* peer closed */
        s_w5.sreg[s][SN_SR] = SS_CLOSE_WAIT;
        s_w5.sreg[s][SN_IR] |= 0x02;             /* DISCON */
        qemu_set_fd_handler(s_w5_conn[s], NULL, NULL, NULL);
        close(s_w5_conn[s]);
        s_w5_conn[s] = -1;
    }
    /* n<0 (EAGAIN): nothing waiting */
}

static void w5_conn_readable(void *opaque){ w5_recv_from_host((int)(intptr_t)opaque); }

/* ---- W5500 UDP datapath (BACnet/IP + other UDP OT protocols) --------------
 * A socket opened with Sn_MR=UDP gets a real host SOCK_DGRAM bound to
 * 127.0.0.1:<Sn_PORT>. Inbound datagrams are framed into the RX ring with the
 * WIZnet 8-byte UDP PACKET-INFO header the driver reads back as remoteIP/Port:
 *   [srcIP(4)][srcPort(2)][len(2)][payload...]
 * SEND takes the destination from Sn_DIPR/Sn_DPORT (set by beginPacket()).
 * Mirrors the TCP bridge above; reuses s_w5_conn[] for the datagram fd so the
 * existing CLOSE path tears it down. */
#define SN_DIPR  0x0C            /* destination IP address (4 bytes)          */
#define SN_DPORT 0x10            /* destination port       (2 bytes)          */

static int w5_is_udp(int s){ return (s_w5.sreg[s][SN_MR] & 0x0f) == 0x02; }

static void w5_udp_recv_from_host(int s)
{
    if (s_w5_conn[s] < 0) return;
    for (;;) {                                       /* drain all queued datagrams */
        uint16_t wr = sn_g16(s, SN_RX_WR), rd = sn_g16(s, SN_RX_RD);
        int used  = (uint16_t)(wr - rd);
        int space = W5_BUFSZ - used;
        if (space < 8 + 1) return;                   /* need header + >=1 data byte */
        uint8_t tmp[W5_BUFSZ];
        int cap = space - 8;                         /* leave room for the header  */
        if (cap > (int)sizeof(tmp)) cap = (int)sizeof(tmp);
        struct sockaddr_in sa; socklen_t sl = sizeof(sa);
        int n = recvfrom(s_w5_conn[s], (char *)tmp, cap, 0, (struct sockaddr *)&sa, &sl);
        if (n <= 0) return;                          /* EAGAIN / no datagram        */
        uint32_t ip = ntohl(sa.sin_addr.s_addr);
        uint16_t sp = ntohs(sa.sin_port);
        uint8_t hdr[8] = { (uint8_t)(ip >> 24), (uint8_t)(ip >> 16),
                           (uint8_t)(ip >> 8),  (uint8_t)ip,
                           (uint8_t)(sp >> 8),  (uint8_t)sp,
                           (uint8_t)(n >> 8),   (uint8_t)n };
        for (int i = 0; i < 8; i++) s_w5.rxbuf[s][(uint16_t)(wr + i)     & (W5_BUFSZ - 1)] = hdr[i];
        for (int i = 0; i < n; i++) s_w5.rxbuf[s][(uint16_t)(wr + 8 + i) & (W5_BUFSZ - 1)] = tmp[i];
        wr += 8 + n;
        sn_s16(s, SN_RX_WR, wr);
        sn_s16(s, SN_RX_RSR, (uint16_t)(wr - rd));
        s_w5.sreg[s][SN_IR] |= 0x04;                 /* RECV */
        if (s_w5dbg) fprintf(stderr, "W5.sock%d UDP RX +%d from %u.%u.%u.%u:%u\n",
                             s, n, hdr[0], hdr[1], hdr[2], hdr[3], sp);
    }
}

static void w5_udp_readable(void *opaque){ w5_udp_recv_from_host((int)(intptr_t)opaque); }

/* Host bind address for the W5500 socket bridge. Default 127.0.0.1 keeps the
 * standalone harnesses (which connect to loopback) working. Set TBD_W5_BIND to
 * "0.0.0.0"/"any" (or a specific IP) so the twin listens on the CORE node's real
 * LAN interface -> the PLC IS its configured IP on the OT-LAN, no socat relay. */
static uint32_t w5_parse_ip(const char *b)
{
    unsigned a0, a1, a2, a3;
    if (sscanf(b, "%u.%u.%u.%u", &a0, &a1, &a2, &a3) == 4 &&
        a0 < 256 && a1 < 256 && a2 < 256 && a3 < 256)
        return (a0 << 24) | (a1 << 16) | (a2 << 8) | a3;   /* host order */
    return INADDR_LOOPBACK;
}
static uint32_t w5_bind_addr(void)
{
    const char *b = getenv("TBD_W5_BIND");
    if (!b || !*b)                                   return INADDR_LOOPBACK;
    if (!strcmp(b, "any") || !strcmp(b, "0.0.0.0"))  return INADDR_ANY;   /* 0 */
    if (!strcmp(b, "lo")  || !strcmp(b, "loopback")) return INADDR_LOOPBACK;
    return w5_parse_ip(b);
}

static void w5_udp_open(int s)
{
    if (s_w5_conn[s] >= 0) {                          /* re-OPEN: drop old socket   */
        qemu_set_fd_handler(s_w5_conn[s], NULL, NULL, NULL);
        close(s_w5_conn[s]);
        s_w5_conn[s] = -1;
    }
    uint16_t port = sn_g16(s, SN_PORT);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(w5_bind_addr());
    sa.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 && s_w5dbg)
        fprintf(stderr, "W5.sock%d UDP bind %08x:%u failed\n", s, w5_bind_addr(), port);
    qemu_socket_set_nonblock(fd);
    s_w5_conn[s] = fd;
    qemu_set_fd_handler(fd, w5_udp_readable, NULL, (void *)(intptr_t)s);
    if (s_w5dbg) fprintf(stderr, "W5.sock%d UDP open %08x:%u\n", s, w5_bind_addr(), port);
}

static void w5_listen_accept(void *opaque);

/* A host listener represents a port, while LISTEN belongs to individual chip
 * sockets. Leave pending host connections queued until the guest has a socket
 * for them; accepting then closing them spuriously resets burst connections. */
static void w5_listener_interest(int li)
{
    int ready = 0;
    if (li < 0 || s_w5_lsn[li].fd < 0) return;
    for (int s = 0; s < W5_NSOCK; s++) {
        if (s_w5.sreg[s][SN_SR] == SS_LISTEN &&
            sn_g16(s, SN_PORT) == s_w5_lsn[li].port) {
            ready = 1;
            break;
        }
    }
    qemu_set_fd_handler(s_w5_lsn[li].fd, ready ? w5_listen_accept : NULL,
                        NULL, (void *)(intptr_t)li);
}

static void w5_listen_accept(void *opaque)
{
    int li = (int)(intptr_t)opaque;
    for (int s = 0; s < W5_NSOCK; s++) {
        if (s_w5.sreg[s][SN_SR] == SS_LISTEN && sn_g16(s, SN_PORT) == s_w5_lsn[li].port) {
            struct sockaddr_in peer;
            socklen_t peer_len = sizeof(peer);
            int cfd = accept(s_w5_lsn[li].fd, (struct sockaddr *)&peer, &peer_len);
            if (cfd < 0) return;
            qemu_socket_set_nonblock(cfd);
            s_w5_conn[s] = cfd;
            memcpy(&s_w5.sreg[s][SN_DIPR], &peer.sin_addr.s_addr, 4);
            sn_s16(s, SN_DPORT, ntohs(peer.sin_port));
            s_w5.sreg[s][SN_SR] = SS_ESTAB;
            s_w5.sreg[s][SN_IR] |= 0x01;         /* CON */
            sn_s16(s, SN_RX_RSR, 0);
            qemu_set_fd_handler(cfd, w5_conn_readable, NULL, (void *)(intptr_t)s);
            w5_listener_interest(li);
            if (s_w5dbg) fprintf(stderr, "W5.sock%d ESTABLISHED (port %u)\n", s, s_w5_lsn[li].port);
            return;
        }
    }
    w5_listener_interest(li);              /* no chip listener: wait for guest LISTEN */
}

static void w5_start_listen(int s)
{
    uint16_t port = sn_g16(s, SN_PORT);
    int li = -1, freei = -1;
    for (int i = 0; i < W5_NSOCK; i++) {
        if (s_w5_lsn[i].fd >= 0 && s_w5_lsn[i].port == port) { li = i; break; }
        if (s_w5_lsn[i].fd < 0 && freei < 0) freei = i;
    }
    if (li < 0 && freei >= 0) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0) {
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_addr.s_addr = htonl(w5_bind_addr());
            sa.sin_port = htons(port);
            if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0 && listen(fd, 8) == 0) {
                qemu_socket_set_nonblock(fd);
                s_w5_lsn[freei].fd = fd;
                s_w5_lsn[freei].port = port;
                li = freei;
                if (s_w5dbg) fprintf(stderr, "W5.listen host %08x:%u (sock%d)\n", w5_bind_addr(), port, s);
            } else {
                close(fd);
            }
        }
    }
    s_w5.sreg[s][SN_SR] = SS_LISTEN;
    w5_listener_interest(li);
}

static void w5_sn_send(int s)
{
    if (w5_is_udp(s)) {                          /* UDP: one datagram to Sn_DIPR:Sn_DPORT */
        if (s_w5_conn[s] >= 0) {
            uint16_t rdp = sn_g16(s, SN_TX_RD), wrp = sn_g16(s, SN_TX_WR);
            uint16_t len = wrp - rdp;
            uint8_t tmp[W5_BUFSZ];
            for (int i = 0; i < len; i++) tmp[i] = s_w5.txbuf[s][(uint16_t)(rdp + i) & (W5_BUFSZ - 1)];
            struct sockaddr_in da;
            memset(&da, 0, sizeof(da));
            da.sin_family = AF_INET;
            da.sin_addr.s_addr = htonl(((uint32_t)s_w5.sreg[s][SN_DIPR]     << 24) |
                                       ((uint32_t)s_w5.sreg[s][SN_DIPR + 1] << 16) |
                                       ((uint32_t)s_w5.sreg[s][SN_DIPR + 2] <<  8) |
                                        (uint32_t)s_w5.sreg[s][SN_DIPR + 3]);
            da.sin_port = htons(sn_g16(s, SN_DPORT));
            ssize_t r = sendto(s_w5_conn[s], (char *)tmp, len, 0,
                               (struct sockaddr *)&da, sizeof(da));
            (void)r;
            sn_s16(s, SN_TX_RD, wrp);
            if (s_w5dbg) fprintf(stderr, "W5.sock%d UDP TX %u bytes -> %u.%u.%u.%u:%u\n",
                                 s, len, s_w5.sreg[s][SN_DIPR], s_w5.sreg[s][SN_DIPR + 1],
                                 s_w5.sreg[s][SN_DIPR + 2], s_w5.sreg[s][SN_DIPR + 3],
                                 sn_g16(s, SN_DPORT));
        }
        sn_s16(s, SN_TX_FSR, W5_BUFSZ);
        s_w5.sreg[s][SN_IR] |= 0x10;             /* SEND_OK */
        return;
    }
    if (s_w5_conn[s] >= 0) {
        uint16_t rdp = sn_g16(s, SN_TX_RD), wrp = sn_g16(s, SN_TX_WR);
        uint16_t len = wrp - rdp;
        uint8_t tmp[W5_BUFSZ];
        for (int i = 0; i < len; i++) tmp[i] = s_w5.txbuf[s][(uint16_t)(rdp + i) & (W5_BUFSZ - 1)];
        int off = 0;
        while (off < len) { int n = send(s_w5_conn[s], (char *)tmp + off, len - off, 0); if (n <= 0) break; off += n; }
        sn_s16(s, SN_TX_RD, wrp);
        if (s_w5dbg) fprintf(stderr, "W5.sock%d TX %u bytes\n", s, len);
    }
    sn_s16(s, SN_TX_FSR, W5_BUFSZ);
    s_w5.sreg[s][SN_IR] |= 0x10;                 /* SEND_OK */
}

/* TCP client: connect() completion (async, non-blocking connect). */
static void w5_conn_writable(void *opaque)
{
    int s = (int)(intptr_t)opaque, fd = s_w5_conn[s];
    if (fd < 0) return;
    int err = 0; socklen_t el = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
    if (err == 0) {
        s_w5.sreg[s][SN_SR] = SS_ESTAB;
        s_w5.sreg[s][SN_IR] |= 0x01;                 /* CON */
        sn_s16(s, SN_RX_RSR, 0);
        qemu_set_fd_handler(fd, w5_conn_readable, NULL, (void *)(intptr_t)s);
        if (s_w5dbg) fprintf(stderr, "W5.sock%d CONNECT established\n", s);
    } else {
        qemu_set_fd_handler(fd, NULL, NULL, NULL); close(fd); s_w5_conn[s] = -1;
        s_w5.sreg[s][SN_SR] = SS_CLOSED; s_w5.sreg[s][SN_IR] |= 0x08;   /* TIMEOUT */
        if (s_w5dbg) fprintf(stderr, "W5.sock%d CONNECT failed err=%d\n", s, err);
    }
}

/* Sn_CR CONNECT: dial out to Sn_DIPR:Sn_DPORT (TCP client — e.g. MQTT to a broker). */
static void w5_start_connect(int s)
{
    uint32_t dip = ((uint32_t)s_w5.sreg[s][SN_DIPR]     << 24) |
                   ((uint32_t)s_w5.sreg[s][SN_DIPR + 1] << 16) |
                   ((uint32_t)s_w5.sreg[s][SN_DIPR + 2] <<  8) |
                    (uint32_t)s_w5.sreg[s][SN_DIPR + 3];
    uint16_t dport = sn_g16(s, SN_DPORT);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;
    qemu_socket_set_nonblock(fd);
    struct sockaddr_in da; memset(&da, 0, sizeof(da));
    da.sin_family = AF_INET; da.sin_addr.s_addr = htonl(dip); da.sin_port = htons(dport);
    s_w5_conn[s] = fd;
    int r = connect(fd, (struct sockaddr *)&da, sizeof(da));
    if (r == 0) {                                    /* immediate (LAN/loopback) */
        s_w5.sreg[s][SN_SR] = SS_ESTAB; s_w5.sreg[s][SN_IR] |= 0x01; sn_s16(s, SN_RX_RSR, 0);
        qemu_set_fd_handler(fd, w5_conn_readable, NULL, (void *)(intptr_t)s);
        if (s_w5dbg) fprintf(stderr, "W5.sock%d CONNECT %u.%u.%u.%u:%u established\n", s,
            s_w5.sreg[s][SN_DIPR], s_w5.sreg[s][SN_DIPR+1], s_w5.sreg[s][SN_DIPR+2], s_w5.sreg[s][SN_DIPR+3], dport);
    } else if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
        s_w5.sreg[s][SN_SR] = SS_INIT;               /* connecting; wait for writable */
        qemu_set_fd_handler(fd, NULL, w5_conn_writable, (void *)(intptr_t)s);
        if (s_w5dbg) fprintf(stderr, "W5.sock%d CONNECT %u.%u.%u.%u:%u in progress\n", s,
            s_w5.sreg[s][SN_DIPR], s_w5.sreg[s][SN_DIPR+1], s_w5.sreg[s][SN_DIPR+2], s_w5.sreg[s][SN_DIPR+3], dport);
    } else {
        close(fd); s_w5_conn[s] = -1; s_w5.sreg[s][SN_SR] = SS_CLOSED;
        if (s_w5dbg) fprintf(stderr, "W5.sock%d CONNECT failed errno=%d\n", s, errno);
    }
}

static void w5_sn_cmd(int s, uint8_t cmd)
{
    switch (cmd) {
    case 0x01:                                   /* OPEN */
        s_w5.sreg[s][SN_SR] = w5_is_udp(s) ? SS_UDP : SS_INIT;
        sn_s16(s, SN_TX_RD, 0); sn_s16(s, SN_TX_WR, 0);
        sn_s16(s, SN_RX_RD, 0); sn_s16(s, SN_RX_WR, 0); sn_s16(s, SN_RX_RSR, 0);
        sn_s16(s, SN_TX_FSR, W5_BUFSZ);
        if (w5_is_udp(s)) w5_udp_open(s);        /* bind a host UDP socket now  */
        break;
    case 0x02: w5_start_listen(s); break;        /* LISTEN */
    case 0x04: w5_start_connect(s); break;       /* CONNECT (TCP client, e.g. MQTT) */
    case 0x08:                                   /* DISCON */
    case 0x10:                                   /* CLOSE */
        if (s_w5_conn[s] >= 0) {
            qemu_set_fd_handler(s_w5_conn[s], NULL, NULL, NULL);
            close(s_w5_conn[s]);
            s_w5_conn[s] = -1;
        }
        s_w5.sreg[s][SN_SR] = SS_CLOSED;
        break;
    case 0x20: w5_sn_send(s); break;             /* SEND */
    case 0x40:                                   /* RECV: driver advanced RX_RD */
        sn_s16(s, SN_RX_RSR, (uint16_t)(sn_g16(s, SN_RX_WR) - sn_g16(s, SN_RX_RD)));
        if (w5_is_udp(s)) w5_udp_recv_from_host(s);  /* pull further datagrams  */
        else              w5_recv_from_host(s);      /* pull further TCP bytes  */
        break;
    default: break;
    }
    s_w5.sreg[s][SN_CR] = 0;                      /* command completes instantly */
}

/* GPIO11 (W5500 chip-select) edge, from the GPIO model. cs_low=1 → CS asserted:
 * a new SPI frame begins, so reset the byte-phase state machine. */
static int s_w5cs_edges = 0;
void tbd_w5500_cs_notify(int cs_low)
{
    if (cs_low) {
        s_w5.phase = 0;
        s_w5cs_edges++;
    }
    if (s_w5dbg && s_w5_trace && s_w5_tracen < 220)
        fprintf(stderr, "  [CS %s]\n", cs_low ? "v" : "^");
}

/* Process one full-duplex byte: consume mosi, return the miso byte. */
static uint8_t tbd_w5500_byte(uint8_t mosi)
{
    switch (s_w5.phase) {
    case 0: s_w5.addr = (uint16_t)mosi << 8; s_w5.phase = 1; return 0;
    case 1: s_w5.addr |= mosi;               s_w5.phase = 2; return 0;
    case 2:
        s_w5.ctrl   = mosi;
        s_w5.bsb    = mosi >> 3;
        s_w5.rwb    = (mosi >> 2) & 1;
        s_w5.region = s_w5.bsb & 3;          /* 0 common,1 reg,2 tx,3 rx        */
        s_w5.sock   = s_w5.bsb >> 2;
        s_w5.data_off = s_w5.addr;
        s_w5.phase  = 3;
        if (s_w5dbg && s_w5_trace && s_w5_tracen < 220) {
            s_w5_tracen++;
            fprintf(stderr, "W5.acc #%d addr=%04x ctrl=%02x %s bsb=%d reg=%d sock=%d\n",
                    s_w5_tracen, s_w5.addr, s_w5.ctrl, s_w5.rwb ? "WR" : "rd",
                    s_w5.bsb, s_w5.region, s_w5.sock);
        }
        return 0;
    default: {                               /* data phase, VDM auto-increment  */
        uint8_t miso = 0;
        uint16_t off = s_w5.data_off++;
        if (s_w5.bsb == 0) {                 /* common register block           */
            if (s_w5.rwb) {
                if (off < 0x100) s_w5.common[off] = mosi;
                /* MR (0x0000) bit7 = S/W reset: self-clears when reset completes
                 * (instant here) so softReset()'s poll returns. */
                if (off == 0x0000 && (mosi & 0x80)) { s_w5.common[0x0000] = mosi & 0x7f; s_w5_trace = 1; }
            } else        { miso = (off < 0x100) ? s_w5.common[off] : 0; }
            if (s_w5dbg && s_w5_trace && s_w5_tracen < 220)
                fprintf(stderr, "   common[%02x] %s %02x\n", off, s_w5.rwb ? "<=" : "=>",
                        s_w5.rwb ? mosi : miso);
        } else if (s_w5.region == 1) {       /* socket register                 */
            int s = s_w5.sock & 7;
            if (s_w5.rwb) {
                if (off < 0x100) s_w5.sreg[s][off] = mosi;
                if (off == SN_CR && mosi) w5_sn_cmd(s, mosi);  /* execute + clear */
            } else        { miso = (off < 0x100) ? s_w5.sreg[s][off] : 0; }
        } else if (s_w5.region == 2) {       /* socket TX buffer                */
            int s = s_w5.sock & 7; uint16_t o = off & (W5_BUFSZ - 1);
            if (s_w5.rwb) s_w5.txbuf[s][o] = mosi; else miso = s_w5.txbuf[s][o];
        } else {                             /* socket RX buffer                */
            int s = s_w5.sock & 7; uint16_t o = off & (W5_BUFSZ - 1);
            if (s_w5.rwb) s_w5.rxbuf[s][o] = mosi; else miso = s_w5.rxbuf[s][o];
        }
        return miso;
    }
    }
}

/* Execute one FSPI full-duplex burst against the W5500: read nbytes of MOSI out
 * of the W0..W15 data registers, feed them through the frame machine, and write
 * the MISO bytes back so the driver reads them. */
static void tbd_w5500_xfer(uint32_t *wreg, int nbytes)
{
    if (s_w5dbg && s_w5_trace && s_w5_tracen < 220)
        fprintf(stderr, "  <xfer %d bytes, phase=%d>\n", nbytes, s_w5.phase);
    for (int i = 0; i < nbytes; i++) {
        uint8_t mosi = (wreg[i / 4] >> (8 * (i % 4))) & 0xff;
        uint8_t miso = tbd_w5500_byte(mosi);
        int wi = i / 4, sh = 8 * (i % 4);
        wreg[wi] = (wreg[wi] & ~(0xffu << sh)) | ((uint32_t)miso << sh);
    }
}

/* ========================= TBD twin: ST7789 LCD =========================
 * The StamPLC's 240x135 IPS panel hangs off the shared FSPI bus (CS=GPIO12),
 * with a data/command select on DC=GPIO6. M5GFX drives it in CPU mode (pixel
 * bytes through the SPI W0..W15 registers, not DMA), so the FSPI interceptor
 * already sees every command and pixel. We decode the ST7789 address-window +
 * RAMWR protocol into an RGB565 framebuffer and periodically dump it to a PPM
 * (path in TBD_LCD_PPM) so the operator screen can be viewed/screenshotted. */
#define LCD_MAX 320
static uint16_t s_lcd_fb[LCD_MAX * LCD_MAX];
static uint8_t  s_lcd_cmd;
static int      s_lcd_pi;                 /* parameter byte index              */
static uint8_t  s_lcd_p[8];
static uint16_t s_lcd_xs, s_lcd_xe, s_lcd_ys, s_lcd_ye, s_lcd_cx, s_lcd_cy;
static int      s_lcd_have_hi;            /* pixel high-byte pending           */
static uint8_t  s_lcd_hi;
static int      s_lcd_minx = LCD_MAX, s_lcd_miny = LCD_MAX, s_lcd_maxx, s_lcd_maxy;
static QEMUTimer *s_lcd_timer;
static const char *s_lcd_ppm;

static void tbd_lcd_plot(uint16_t c)
{
    if (s_lcd_cx < LCD_MAX && s_lcd_cy < LCD_MAX) {
        s_lcd_fb[s_lcd_cy * LCD_MAX + s_lcd_cx] = c;
        if (s_lcd_cx < s_lcd_minx) s_lcd_minx = s_lcd_cx;
        if (s_lcd_cx > s_lcd_maxx) s_lcd_maxx = s_lcd_cx;
        if (s_lcd_cy < s_lcd_miny) s_lcd_miny = s_lcd_cy;
        if (s_lcd_cy > s_lcd_maxy) s_lcd_maxy = s_lcd_cy;
    }
    if (s_lcd_cx >= s_lcd_xe) { s_lcd_cx = s_lcd_xs; if (s_lcd_cy < 0xFFFF) s_lcd_cy++; }
    else s_lcd_cx++;
}

/* One FSPI burst to the LCD. dc=0 command byte(s); dc=1 parameters / pixels. */
static void tbd_lcd_xfer(int dc, const uint32_t *wreg, int nbytes)
{
    for (int i = 0; i < nbytes; i++) {
        uint8_t b = (wreg[i / 4] >> (8 * (i % 4))) & 0xff;
        if (dc == 0) {                    /* command */
            s_lcd_cmd = b; s_lcd_pi = 0; s_lcd_have_hi = 0;
            if (b == 0x2C) { s_lcd_cx = s_lcd_xs; s_lcd_cy = s_lcd_ys; } /* RAMWR */
            continue;
        }
        switch (s_lcd_cmd) {              /* data phase */
        case 0x2A:                        /* CASET: XS(2) XE(2) */
            if (s_lcd_pi < 4) s_lcd_p[s_lcd_pi++] = b;
            if (s_lcd_pi == 4) { s_lcd_xs = (s_lcd_p[0] << 8) | s_lcd_p[1];
                                 s_lcd_xe = (s_lcd_p[2] << 8) | s_lcd_p[3]; }
            break;
        case 0x2B:                        /* RASET: YS(2) YE(2) */
            if (s_lcd_pi < 4) s_lcd_p[s_lcd_pi++] = b;
            if (s_lcd_pi == 4) { s_lcd_ys = (s_lcd_p[0] << 8) | s_lcd_p[1];
                                 s_lcd_ye = (s_lcd_p[2] << 8) | s_lcd_p[3]; }
            break;
        case 0x2C: case 0x3C:             /* RAMWR / write-continue: RGB565 px */
            if (!s_lcd_have_hi) { s_lcd_hi = b; s_lcd_have_hi = 1; }
            else { tbd_lcd_plot(((uint16_t)s_lcd_hi << 8) | b); s_lcd_have_hi = 0; }
            break;
        default: break;                   /* MADCTL/COLMOD/etc: consumed, ignored */
        }
    }
}

static void tbd_lcd_dump(void *opaque)
{
    if (s_lcd_ppm && s_lcd_maxx >= s_lcd_minx && s_lcd_maxy >= s_lcd_miny) {
        int w = s_lcd_maxx - s_lcd_minx + 1, h = s_lcd_maxy - s_lcd_miny + 1;
        FILE *f = fopen(s_lcd_ppm, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (int y = s_lcd_miny; y <= s_lcd_maxy; y++)
                for (int x = s_lcd_minx; x <= s_lcd_maxx; x++) {
                    uint16_t c = s_lcd_fb[y * LCD_MAX + x];
                    uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 0x1F) * 255 / 31),
                                       (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
                                       (uint8_t)((c & 0x1F) * 255 / 31) };
                    fwrite(rgb, 1, 3, f);
                }
            fclose(f);
        }
    }
    timer_mod(s_lcd_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 500);
}

static uint64_t tbd_fspi_read(void *opaque, hwaddr addr, unsigned size)
{
    uint32_t off = (uint32_t)(addr & 0xFFCU);
    uint32_t r;

    const char *cs = tbd_fspi_active_cs();
    if (off == A_SPI_CMD) {
        /* Transaction always already complete in our instant model: report every
         * command/start bit clear so the driver's busy-poll returns immediately
         * (the original stub returned 0 here — RAM-backing it hung early LCD I/O). */
        r = 0;
    } else if (off >= A_SPI_W0 && off <= A_SPI_W15) {
        if (!strcmp(cs, "LCD")) {
            r = 0x00000085u;                 /* ST7789 panel ID → board detect */
        } else {
            r = s_fspi_reg[off / 4];         /* W5500 MISO (RAM until modeled) */
        }
    } else {
        r = s_fspi_reg[off / 4];
    }
    if (s_w5dbg && strcmp(cs, "-")) {        /* only real device transactions */
        fprintf(stderr, "W5DBG R off=%03x val=%08x cs=%s\n", off, r, cs);
    }
    return r;
}

static void tbd_fspi_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    uint32_t off = (uint32_t)(addr & 0xFFCU);
    const char *cs = tbd_fspi_active_cs();
    s_fspi_reg[off / 4] = (uint32_t)val;
    if (s_w5dbg && strcmp(cs, "-")) {        /* only real device transactions */
        fprintf(stderr, "W5DBG W off=%03x val=%08x cs=%s\n", off, (uint32_t)val, cs);
    }
    /* Transaction trigger (SPI_CMD USR/CONF_BITLEN start) while the W5500 is
     * chip-selected: run the full-duplex burst through the W5500 model, leaving
     * the MISO bytes in W0.. for the driver to read back. */
    /* SPI_USR (bit 24) actually starts the transfer; SPI_UPDATE (bit 23) only
     * latches config — triggering on both ran every burst through the model
     * twice and desynced the frame. Trigger on SPI_USR alone. */
    if (off == A_SPI_CMD && (val & 0x01000000u) && !strcmp(cs, "W5500")) {
        uint32_t dlen = s_fspi_reg[A_SPI_MS_DLEN / 4];
        int nbytes = (int)((dlen + 1) / 8);
        if (nbytes < 1) nbytes = 1;
        if (nbytes > 64) nbytes = 64;
        tbd_w5500_xfer(&s_fspi_reg[A_SPI_W0 / 4], nbytes);
    } else if (off == A_SPI_CMD && (val & 0x01000000u) && !strcmp(cs, "LCD")) {
        /* DC (GPIO6): low = command, high = data/pixels. */
        uint32_t gout = 0;
        cpu_physical_memory_read(0x60004004, &gout, 4);
        int dc = (gout >> 6) & 1;
        uint32_t dlen = s_fspi_reg[A_SPI_MS_DLEN / 4];
        int nbytes = (int)((dlen + 1) / 8);
        if (nbytes < 1) nbytes = 1;
        if (nbytes > 64) nbytes = 64;
        tbd_lcd_xfer(dc, &s_fspi_reg[A_SPI_W0 / 4], nbytes);
    }
}
static const MemoryRegionOps tbd_fspi_ops = {
    .read = tbd_fspi_read,
    .write = tbd_fspi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

/* TBD twin: RAM-backed LEDC with buzzer tone capture. The firmware plays tones
 * on the piezo buzzer (STAMPLC_PIN_BUZZ = GPIO44) via Arduino tone()->LEDC; the
 * ledc_ll clk read also abort()s unless LEDC_CONF.apb_clk_sel reads back 1/2/3,
 * so writes must persist. On top of persistence we decode the LEDC timer config
 * when a channel's signal output is enabled and log the tone frequency — turning
 * the otherwise-silent buzzer into an observable twin telemetry event (boot beep,
 * button feedback, mode-change chirps). No real audio is produced. Each tone is
 * appended to the file named by TBD_BUZZER_LOG (else stderr).
 *
 * ESP32-S3 LEDC layout: CHn_CONF0 @ n*0x14 (TIMER_SEL[1:0], SIG_OUT_EN bit2);
 * TIMERn_CONF @ 0xA0+n*8 (DUTY_RES[3:0], CLK_DIV Q10.8 [21:4]); CONF @ 0xD0
 * (CLK_SEL[1:0]: 1=APB 80MHz, 2=RC_FAST, 3=XTAL 40MHz). Buzzer freq =
 * src*256 / (clk_div * 2^duty_res). (Field split verified against the firmware's
 * tone(1500,..) boot beep decoding to 1500 Hz on the XTAL source.) */
static uint32_t tbd_ledc_regs[0x800 / 4];
static uint64_t tbd_ledc_read(void *opaque, hwaddr addr, unsigned size)
{
    return tbd_ledc_regs[(addr & 0x7FCU) >> 2];
}
static void tbd_ledc_buzzer_event(int ch)
{
    uint32_t conf0 = tbd_ledc_regs[(ch * 0x14) / 4];
    int timer = conf0 & 0x3;
    uint32_t tconf = tbd_ledc_regs[(0xA0 + timer * 8) / 4];
    uint32_t duty_res = tconf & 0xF;                 /* DUTY_RES[3:0]           */
    uint32_t clk_div  = (tconf >> 4) & 0x3FFFF;      /* CLK_DIV Q10.8 [21:4]    */
    uint32_t sel = tbd_ledc_regs[0xD0 / 4] & 0x3;
    double src = (sel == 3) ? 40000000.0 : (sel == 2) ? 17500000.0 : 80000000.0;
    const char *srcname = (sel == 3) ? "XTAL" : (sel == 2) ? "RC_FAST" : "APB";
    const char *path = getenv("TBD_BUZZER_LOG");
    FILE *fp = path ? fopen(path, "a") : stderr;
    if (!fp) {
        fp = stderr;
    }
    if (clk_div && duty_res) {
        double freq = src * 256.0 / ((double)clk_div * (double)(1u << duty_res));
        fprintf(fp, "[BUZZER] tone ~%.0f Hz  (ch%d timer%d res=%u div=%.3f src=%s)\n",
                freq, ch, timer, duty_res, clk_div / 256.0, srcname);
    } else {
        fprintf(fp, "[BUZZER] tone (ch%d timer%d not yet configured)\n", ch, timer);
    }
    fflush(fp);
    if (fp != stderr) {
        fclose(fp);
    }
}
static void tbd_ledc_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    uint32_t idx = (addr & 0x7FCU) >> 2;
    uint32_t off = addr & 0x7FCU;
    uint32_t before = tbd_ledc_regs[idx];
    tbd_ledc_regs[idx] = (uint32_t)val;
    /* Every tone() re-asserts a non-zero duty on the buzzer's LEDC channel (the
     * channel stays attached, so SIG_OUT_EN only rises once — the duty write is
     * what marks each individual beep). CHn_DUTY is at 0x08 + n*0x14. The LCD
     * backlight is on the PI4IOE, not LEDC, so LEDC traffic here is buzzer-only. */
    bool is_duty = (off < 8 * 0x14) && ((off % 0x14) == 0x08);
    if (is_duty && (val & 0x1FFFFFu) && !(before & 0x1FFFFFu)) {
        tbd_ledc_buzzer_event((int)(off / 0x14));
    }
}
static const MemoryRegionOps tbd_ledc_ops = {
    .read = tbd_ledc_read,
    .write = tbd_ledc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* ======================================================================== */
/*  TBD twin: passive CAN bus monitor                                     */
/* ------------------------------------------------------------------------ */
/*  A virtual CAN analyzer sitting on the StamPLC's CAN segment. It registers */
/*  as a second client on the QEMU can-bus (when one is attached with         */
/*  -object can-bus,id=canbus0), so every frame the TWAI/SJA1000 controller   */
/*  transmits is captured — giving range labs a CAN sniffer and proving the   */
/*  controller's transmit path reaches the bus end-to-end. Each frame is      */
/*  appended to the file named by TBD_CAN_LOG (else stderr). Purely passive: */
/*  it never sources frames, so it cannot perturb a firmware that uses CAN.   */
static bool tbd_canmon_can_receive(CanBusClientState *client)
{
    return true;
}

static ssize_t tbd_canmon_receive(CanBusClientState *client,
                                    const qemu_can_frame *frames, size_t frames_cnt)
{
    const char *path = getenv("TBD_CAN_LOG");
    FILE *fp = path ? fopen(path, "a") : stderr;
    if (!fp) {
        fp = stderr;
    }
    for (size_t i = 0; i < frames_cnt; i++) {
        const qemu_can_frame *f = &frames[i];
        bool eff = f->can_id & QEMU_CAN_EFF_FLAG;
        uint32_t id = eff ? (f->can_id & QEMU_CAN_EFF_MASK)
                          : (f->can_id & QEMU_CAN_SFF_MASK);
        fprintf(fp, "[CANMON] %s id=0x%03X dlc=%u data=",
                eff ? "ext" : "std", id, f->can_dlc);
        for (unsigned b = 0; b < f->can_dlc && b < 8; b++) {
            fprintf(fp, "%02X ", f->data[b]);
        }
        fprintf(fp, "%s\n", (f->can_id & QEMU_CAN_RTR_FLAG) ? "(RTR)" : "");
    }
    fflush(fp);
    if (fp != stderr) {
        fclose(fp);
    }
    return frames_cnt;
}

static CanBusClientInfo tbd_canmon_info = {
    .can_receive = tbd_canmon_can_receive,
    .receive     = tbd_canmon_receive,
};

/* Attach the monitor to the CAN bus (a single static client for the machine). */
static void tbd_canmon_attach(Object *canbus)
{
    static CanBusClientState mon;
    static bool attached;
    if (attached) {
        return;
    }
    mon.info = &tbd_canmon_info;
    if (can_bus_insert_client(CAN_BUS(canbus), &mon) == 0) {
        attached = true;
    }
}

static void esp32s3_soc_realize(DeviceState *dev, Error **errp)
{
    Esp32s3SocState *s = ESP32S3_SOC(dev);
    MachineState *ms = MACHINE(qdev_get_machine());
    DeviceState* intmatrix_dev = DEVICE(&s->intmatrix);
    MemoryRegion *sys_mem = get_system_memory();

    const struct MemmapEntry *memmap = esp32s3_memmap;

    MemoryRegion *iram = g_new(MemoryRegion, 1);
    MemoryRegion *rtcslow = g_new(MemoryRegion, 1);
    MemoryRegion *rtcfast = g_new(MemoryRegion, 1);

    for (int i = 0; i < ms->smp.cpus; ++i) {
        MemoryRegion *drom = g_new(MemoryRegion, 1);
        MemoryRegion *irom = g_new(MemoryRegion, 1);

        char name[20];
        snprintf(name, sizeof(name), "esp32s3.irom.cpu%d", i);
        memory_region_init_rom(irom, NULL, name, memmap[ESP32S3_MEMREGION_IROM].size, &error_fatal);
        memory_region_add_subregion(&s->cpu_specific_mem[i], memmap[ESP32S3_MEMREGION_IROM].base, irom);

        const hwaddr offset_in_orig = 0x40000;
        snprintf(name, sizeof(name), "esp32s3.drom.cpu%d", i);
        memory_region_init_alias(drom, NULL, name, irom, offset_in_orig, memmap[ESP32S3_MEMREGION_DROM].size);
        memory_region_add_subregion(sys_mem, memmap[ESP32S3_MEMREGION_DROM].base, drom);
    }


    memory_region_init_ram(iram, NULL, "esp32s3.iram",
                           memmap[ESP32S3_MEMREGION_IRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32S3_MEMREGION_IRAM].base, iram);

    memory_region_init_ram(rtcslow, NULL, "esp32s3.rtcslow",
                           memmap[ESP32S3_MEMREGION_RTCSLOW].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32S3_MEMREGION_RTCSLOW].base, rtcslow);

    memory_region_init_ram(rtcfast, NULL, "esp32s3.rtcfast",
                           memmap[ESP32S3_MEMREGION_RTCFAST].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32S3_MEMREGION_RTCFAST].base, rtcfast);

    for (int i = 0; i < ms->smp.cpus; ++i) {
        qdev_realize(DEVICE(&s->cpu[i]), NULL, &error_fatal);
    }


    for (int i = 0; i < ESP32S3_CPU_COUNT; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "cpu%d", i);
        object_property_set_link(OBJECT(&s->intmatrix), name, OBJECT(qemu_get_cpu(i)), &error_abort);
    }
    qdev_realize(DEVICE(&s->intmatrix), &s->periph_bus, &error_fatal);


    qdev_realize(DEVICE(&s->rtc_cntl), &s->rtc_bus, &error_fatal);
    esp32s3_soc_add_periph_device(sys_mem, &s->rtc_cntl, DR_REG_RTCCNTL_BASE);

    qdev_connect_gpio_out_named(DEVICE(&s->rtc_cntl), ESP32S3_RTC_DIG_RESET_GPIO, 0,
                                qdev_get_gpio_in_named(dev, ESP32S3_RTC_DIG_RESET_GPIO, 0));
    qdev_connect_gpio_out_named(DEVICE(&s->rtc_cntl), ESP32S3_RTC_CLK_UPDATE_GPIO, 0,
                                qdev_get_gpio_in_named(dev, ESP32S3_RTC_CLK_UPDATE_GPIO, 0));
    for (int i = 0; i < ms->smp.cpus; ++i) {
        qdev_connect_gpio_out_named(DEVICE(&s->rtc_cntl), ESP32S3_RTC_CPU_RESET_GPIO, i,
                                    qdev_get_gpio_in_named(dev, ESP32S3_RTC_CPU_RESET_GPIO, i));
        qdev_connect_gpio_out_named(DEVICE(&s->rtc_cntl), ESP32S3_RTC_CPU_STALL_GPIO, i,
                                    qdev_get_gpio_in_named(dev, ESP32S3_RTC_CPU_STALL_GPIO, i));
    }

    for (int i = 0; i < ESP32S3_UART_COUNT; ++i) {
        const hwaddr uart_base[] = {DR_REG_UART_BASE, DR_REG_UART1_BASE, DR_REG_UART2_BASE};
        qdev_realize(DEVICE(&s->uart[i]), &s->periph_bus, &error_fatal);
        esp32s3_soc_add_periph_device(sys_mem, &s->uart[i], uart_base[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           qdev_get_gpio_in(intmatrix_dev, ETS_UART0_INTR_SOURCE + i));
    }

    qdev_realize(DEVICE(&s->sdmmc), &s->periph_bus, &error_fatal);
    esp32s3_soc_add_periph_device(sys_mem, &s->sdmmc, DR_REG_SDMMC_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdmmc), 0,
                       qdev_get_gpio_in(intmatrix_dev, ETS_SDIO_HOST_INTR_SOURCE));

    /* Emulation of APB_CTRL_DATE_REG, needed for ECO3 revision detection.
     * This is a small hack to avoid creating a whole new device just to emulate one
     * register.
     */
    const hwaddr apb_ctrl_regs = DR_REG_APB_CTRL_BASE;
    MemoryRegion *apbctrl_mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(apbctrl_mem, NULL, "esp32s3.apbctrl", 0x400 /* bytes */, &error_fatal);
    memory_region_add_subregion(sys_mem, apb_ctrl_regs, apbctrl_mem);
    uint32_t apb_ctrl_date_reg_val = 0x16042000 | 0x80000000;  /* MSB indicates ECO3 silicon revision */
    uint32_t qemu_sig = RGB_QEMU_ORIGIN;
    cpu_physical_memory_write(apb_ctrl_regs + 0x7c, &apb_ctrl_date_reg_val, 4);
    cpu_physical_memory_write(apb_ctrl_regs + RGB_QEMU_ORIGIN_REG, &qemu_sig, 4);

    qemu_register_reset((QEMUResetHandler*) esp32s3_soc_reset, dev);

    /* TWAI realization */
    {
        /* TBD twin: the StamPLC's CAN port (TX 42 / RX 43) is this TWAI/SJA1000
         * controller. Connect it to a QEMU CAN bus if one was provided on the
         * command line (-object can-bus,id=canbus0), so frames the controller
         * transmits reach the bus (and a host peer via -object can-host-socketcan
         * where the platform allows a vcan). The shipped PLC firmware does not
         * drive CAN, so absent a bus the controller is still present and
         * register-addressable and the default boot is unchanged. When a bus is
         * present we also attach a passive monitor (a virtual CAN analyzer) so
         * transmitted frames are captured for labs and bring-up tests. */
        Object *canbus = object_resolve_path_type("", TYPE_CAN_BUS, NULL);
        if (canbus) {
            object_property_set_link(OBJECT(&s->twai), "canbus", canbus,
                                     &error_fatal);
            tbd_canmon_attach(canbus);
        }
        /* Initialize and realize the TWAI device */
        sysbus_realize(SYS_BUS_DEVICE(&s->twai), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->twai), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_TWAI_BASE, mr, 0);
        /* Connect TWAI interrupt to the interrupt matrix */
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->twai), 0,
                          qdev_get_gpio_in(intmatrix_dev, ETS_TWAI_INTR_SOURCE));
    }

    /* I2C controllers + TBD StamPLC internal-bus device models (RB1.2). */
    {
        const hwaddr i2c_base[2] = { DR_REG_I2C_EXT_BASE, DR_REG_I2C1_EXT_BASE };
        for (int i = 0; i < 2; i++) {
            s->i2c[i].s3_mode = true;   /* S3 I2C command-opcode numbering */
            sysbus_realize(SYS_BUS_DEVICE(&s->i2c[i]), &error_fatal);
            MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->i2c[i]), 0);
            memory_region_add_subregion_overlap(sys_mem, i2c_base[i], mr, 0);
            sysbus_connect_irq(SYS_BUS_DEVICE(&s->i2c[i]), 0,
                               qdev_get_gpio_in(intmatrix_dev, ETS_I2C_EXT0_INTR_SOURCE + i));
        }
        /* Attach the four StamPLC chips on I2C0 as named children => /machine/<name>. */
        I2CBus *tbd_bus = I2C_BUS(qdev_get_child_bus(DEVICE(&s->i2c[0]), "i2c"));
        static const struct { const char *type; const char *name; uint8_t addr; } tbd_devs[] = {
            { "tbd-aw9523", "aw9523", 0x59 }, { "tbd-ina226", "ina226", 0x40 },
            { "tbd-lm75b",  "lm75b",  0x48 }, { "tbd-rx8130", "rx8130", 0x32 },
            { "tbd-pi4ioe", "pi4ioe", 0x43 },   /* IO Expander A: buttons/LEDs/backlight (Wall 5) */
            { "tbd-ioexp",  "ioexp",  0x20 },   /* StamPLC IO expansion pack: 2 analog-in + relays */
        };
        DeviceState *aw = NULL, *ina = NULL, *lm = NULL, *ioe = NULL;
        for (int i = 0; i < (int)ARRAY_SIZE(tbd_devs); i++) {
            DeviceState *cds = qdev_new(tbd_devs[i].type);
            qdev_prop_set_uint8(cds, "address", tbd_devs[i].addr);
            object_property_add_child(OBJECT(s), tbd_devs[i].name, OBJECT(cds));
            i2c_slave_realize_and_unref(I2C_SLAVE(cds), tbd_bus, &error_fatal);
            if (tbd_devs[i].addr == 0x59) aw  = cds;
            if (tbd_devs[i].addr == 0x40) ina = cds;
            if (tbd_devs[i].addr == 0x48) lm  = cds;
            if (tbd_devs[i].addr == 0x20) ioe = cds;
        }
        /* TBD twin: internal process physics — relays (AW9523) drive the sensors
         * (INA226 current/voltage, LM75B temperature) so the Modbus image shows a
         * live process and an unauth coil write has a visible consequence. Set
         * TBD_NOPHYS to run flat (e.g. when an external twin_bridge drives the
         * sensors via QMP qom-set instead). */
        if (aw && ina && lm && !getenv("TBD_NOPHYS")) {
            tbd_physics_register(I2C_SLAVE(aw), I2C_SLAVE(ina), I2C_SLAVE(lm),
                                   ioe ? I2C_SLAVE(ioe) : NULL);
        }

        /* Grove ENV (Bosch BME680) on the *external* Grove RED port = I2C1 @0x77.
         * Present when the firmware is built -DENABLE_GROVE_UNITS; the seeded twin
         * reports a fixed 24.0 C / 45 % RH / 1013 hPa so [GROVE] env_ok=1. The
         * on-board chips above live on I2C0 (tbd_bus); the Grove port is I2C1. */
        {
            I2CBus *grove_bus = I2C_BUS(qdev_get_child_bus(DEVICE(&s->i2c[1]), "i2c"));
            /* Optional test fixture; default direct-ENV topology is unchanged.
             * Synthetic protocol fixtures, not calibrated gas/optical process models. */
            const char *pahub = getenv("TBD_PAHUB");
            /* Observed bench routing; only supported sensors are represented.
             * MQ remains absent. NCIR and ToF4M are separately opt-in protocol fixtures. */
            bool bench=pahub && !strcmp(pahub,"bench-20261002");
            if (pahub) {
                I2CSlave *mux = i2c_slave_create_simple(grove_bus, TYPE_PCA9548, 0x70);
                for (int i = 0; i < 3; ++i) {
                    if (i == 0 && (bench || !strcmp(pahub, "missing-heart"))) continue;
                    DeviceState *unit = qdev_new("tbd-unit-id");
                    qdev_prop_set_uint8(unit, "address", i == 1 ? 0x58 : 0x57);
                    qdev_prop_set_uint8(unit, "kind", i == 1 ? 1 : 0);
                    if(bench && i==2)qdev_prop_set_uint8(unit,"revision",5);
                    qdev_prop_set_bit(unit, "crc-error", i == 1 && !strcmp(pahub, "crc-error"));
                    qdev_prop_set_bit(unit, "measure-crc-error", i == 1 && !strcmp(pahub, "measure-crc"));
                    qdev_prop_set_bit(unit, "force-overflow", i != 1 && !strcmp(pahub, "fifo-overflow"));
                    qdev_prop_set_bit(unit, "force-burst", i != 1 && !strcmp(pahub, "fifo-burst"));
                    i2c_slave_realize_and_unref(I2C_SLAVE(unit),
                        pca954x_i2c_get_bus(mux, i == 2 ? 5 : (bench ? 4 : i)), &error_fatal);
                }
                /* Opt-in ToF protocol fixtures; synthetic values, no optical physics. */
                const char *tof = getenv("TBD_TOF4M");
                if(tof && (!bench || (strcmp(tof,"normal") && strcmp(tof,"wrong-model") && strcmp(tof,"ranging") && strcmp(tof,"timeout") && strcmp(tof,"range-error")))) {
                    error_report("TBD_TOF4M requires bench-20261002 and normal, wrong-model, ranging, timeout or range-error");
                    exit(1);
                }
                if(bench && tof) {
                    DeviceState *unit=qdev_new("tbd-unit-id");
                    qdev_prop_set_uint8(unit,"address",0x29);
                    qdev_prop_set_uint8(unit,"kind",3);
                    qdev_prop_set_bit(unit,"tof-ranging",strcmp(tof,"normal") && strcmp(tof,"wrong-model"));
                    qdev_prop_set_bit(unit,"tof-never-ready",!strcmp(tof,"timeout"));
                    qdev_prop_set_uint8(unit,"tof-status",!strcmp(tof,"range-error")?4:9);
                    qdev_prop_set_uint16(unit,"tof-model",strcmp(tof,"wrong-model")?0xeacc:0x0000);
                    i2c_slave_realize_and_unref(I2C_SLAVE(unit),pca954x_i2c_get_bus(mux,0),&error_fatal);
                }
                /* Opt-in preserves historical bench fixture results. The sensor
                 * is synthetic, not a captured physical identity/temperature. */
                const char *ncir = getenv("TBD_NCIR");
                if (ncir && (!bench || (strcmp(ncir, "normal") &&
                    strcmp(ncir, "identity-pec") && strcmp(ncir, "measurement-pec") &&
                    strcmp(ncir, "sensor-error")))) {
                    error_report("TBD_NCIR requires bench-20261002 and a known NCIR profile");
                    exit(1);
                }
                if (bench && ncir) {
                    DeviceState *unit = qdev_new("tbd-unit-id");
                    qdev_prop_set_uint8(unit, "address", 0x5a);
                    qdev_prop_set_uint8(unit, "kind", 2);
                    qdev_prop_set_bit(unit, "crc-error", !strcmp(ncir, "identity-pec"));
                    qdev_prop_set_bit(unit, "measure-crc-error", !strcmp(ncir, "measurement-pec"));
                    qdev_prop_set_bit(unit, "ncir-sensor-error", !strcmp(ncir, "sensor-error"));
                    object_property_add_child(OBJECT(s), "ncir", OBJECT(unit));
                    i2c_slave_realize_and_unref(I2C_SLAVE(unit),
                        pca954x_i2c_get_bus(mux, 1), &error_fatal);
                }
                if(!bench)i2c_slave_create_simple(tbd_bus, "tbd-ioexp", 0x2f);
                grove_bus = pca954x_i2c_get_bus(mux, bench ? 3 : 2);
            }
            DeviceState *bme = qdev_new("tbd-bme680");
            qdev_prop_set_uint8(bme, "address", 0x77);
            if(bench)qdev_prop_set_uint8(bme,"variant",1);
            object_property_add_child(OBJECT(s), "bme680", OBJECT(bme));
            i2c_slave_realize_and_unref(I2C_SLAVE(bme), grove_bus, &error_fatal);
        }
    }

    /* TBD twin: SAR-ADC done stub so M5Unified's ADC calibration doesn't hang.
     * The real poll is in the SENS/RTC ADC domain (0x6000880C bit 16); also map
     * APB_SARADC for completeness. Overlap priority 100 to win over rtc_cntl. */
    {
        MemoryRegion *sens = g_new(MemoryRegion, 1);
        memory_region_init_io(sens, OBJECT(s), &tbd_saradc_ops, s,
                              "tbd.sens_adc", 0x400);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SENS_BASE, sens, 100);
        MemoryRegion *saradc = g_new(MemoryRegion, 1);
        memory_region_init_io(saradc, OBJECT(s), &tbd_saradc_ops, s,
                              "tbd.saradc", 0x1000);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_APB_SARADC_BASE,
                                            saradc, 100);
    }

    /* TBD twin: GP-SPI panel-ID stub (SPI2/SPI3) for M5GFX board detection. */
    {
        s_w5dbg = (getenv("TBD_W5DBG") != NULL);
        tbd_w5500_init_regs();
        /* LCD framebuffer dump (ST7789 → PPM) when TBD_LCD_PPM is set. */
        s_lcd_ppm = getenv("TBD_LCD_PPM");
        if (s_lcd_ppm) {
            s_lcd_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, tbd_lcd_dump, NULL);
            timer_mod(s_lcd_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 800);
        }
        MemoryRegion *spi2 = g_new(MemoryRegion, 1);
        memory_region_init_io(spi2, OBJECT(s), &tbd_fspi_ops, s,
                              "tbd.spi2_fspi", 0x1000);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SPI2_BASE, spi2, 100);
        MemoryRegion *spi3 = g_new(MemoryRegion, 1);
        memory_region_init_io(spi3, OBJECT(s), &tbd_spi_id_ops, s,
                              "tbd.spi3_id", 0x1000);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SPI3_BASE, spi3, 100);
    }

    /* TBD twin: RAM-backed LEDC stub so the startup-tone LEDC init doesn't abort. */
    {
        MemoryRegion *ledc = g_new(MemoryRegion, 1);
        memory_region_init_io(ledc, OBJECT(s), &tbd_ledc_ops, s,
                              "tbd.ledc", 0x800);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_LEDC_BASE, ledc, 100);
    }
}


static uint64_t esp32s3_io_read(void *opaque, hwaddr addr, unsigned int size)
{
#if ESP32S3_IO_WARNING
    warn_report("[ESP32-S3] Unsupported read to $%08lx, size = %i\n", ESP32S3_IO_START_ADDR + addr, size);
#endif
    return 0;
}


static void esp32s3_io_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
#if ESP32S3_IO_WARNING
        warn_report("[ESP32-S3] Unsupported write $%08lx = %08lx\n", ESP32S3_IO_START_ADDR + addr, value);
#endif
}


/* Define operations for I/OS */
static const MemoryRegionOps esp32s3_io_ops = {
    .read =  esp32s3_io_read,
    .write = esp32s3_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};



static void esp32s3_soc_init(Object *obj)
{
    Esp32s3SocState *s = ESP32S3_SOC(obj);
    MachineState *ms = MACHINE(qdev_get_machine());
    char name[16];
    MemoryRegion *system_memory = get_system_memory();


    qbus_init(&s->periph_bus, sizeof(s->periph_bus),
                        TYPE_SYSTEM_BUS, DEVICE(s), "esp32-periph-bus");
    qbus_init(&s->rtc_bus, sizeof(s->rtc_bus),
                        TYPE_SYSTEM_BUS, DEVICE(s), "esp32-rtc-bus");

    for (int i = 0; i < ms->smp.cpus; ++i) {
        snprintf(name, sizeof(name), "cpu%d", i);

        object_initialize_child(obj, name, &s->cpu[i], TYPE_ESP32S3_CPU);
        // Allocate memory for TIE registers
        s->cpu[i].env.ext = qemu_memalign(16, sizeof(CPUXtensaEsp32s3State));

        if (i == 0)
        {
            s->cpu[i].env.sregs[PRID] = 0xcdcd;
        }
        if (i == 1)
        {
            s->cpu[i].env.sregs[PRID] = 0xabab;
        }

        snprintf(name, sizeof(name), "cpu%d-mem", i);
        memory_region_init(&s->cpu_specific_mem[i], NULL, name, UINT32_MAX);

        CPUState* cs = CPU(&s->cpu[i]);
        cs->num_ases = 1;
        cpu_address_space_init(cs, 0, "cpu-memory", &s->cpu_specific_mem[i]);

        MemoryRegion *cpu_view_sysmem = g_new(MemoryRegion, 1);
        snprintf(name, sizeof(name), "cpu%d-sysmem", i);
        memory_region_init_alias(cpu_view_sysmem, NULL, name, system_memory, 0, UINT32_MAX);
        memory_region_add_subregion_overlap(&s->cpu_specific_mem[i], 0, cpu_view_sysmem, 0);
        cs->memory = &s->cpu_specific_mem[i];
    }

    for (int i = 0; i < ESP32S3_UART_COUNT; ++i) {
        snprintf(name, sizeof(name), "uart%d", i);
        object_initialize_child(obj, name, &s->uart[i], TYPE_ESP32S3_UART);
    }

    object_property_add_alias(obj, "serial0", OBJECT(&s->uart[0]), "chardev");
    object_property_add_alias(obj, "serial1", OBJECT(&s->uart[1]), "chardev");
    object_property_add_alias(obj, "serial2", OBJECT(&s->uart[2]), "chardev");
    qdev_prop_set_chr(DEVICE(&s->uart[0]), "chardev", serial_hd(0));
    qdev_prop_set_chr(DEVICE(&s->uart[1]), "chardev", serial_hd(1));
    /* Keep serial_hd(2) for USB-JTAG compatibility. Port C's independent
     * hardware UART2 gets the fourth backend, without sharing UART1/RS-485. */
    qdev_prop_set_chr(DEVICE(&s->uart[2]), "chardev", serial_hd(3));

    object_initialize_child(obj, "intmatrix", &s->intmatrix, TYPE_ESP32S3_INTMATRIX);

    object_initialize_child(obj, "rtc_cntl", &s->rtc_cntl, TYPE_ESP32S3_RTC_CNTL);

    qdev_init_gpio_in_named(DEVICE(s), esp32s3_dig_reset,  ESP32S3_RTC_DIG_RESET_GPIO, 1);
    qdev_init_gpio_in_named(DEVICE(s), esp32s3_cpu_reset,  ESP32S3_RTC_CPU_RESET_GPIO, ESP32S3_CPU_COUNT);
    qdev_init_gpio_in_named(DEVICE(s), esp32s3_cpu_stall,  ESP32S3_RTC_CPU_STALL_GPIO, ESP32S3_CPU_COUNT);
    qdev_init_gpio_in_named(DEVICE(s), esp32s3_clk_update, ESP32S3_RTC_CLK_UPDATE_GPIO, 1);

    object_initialize_child(obj, "twai", &s->twai, TYPE_ESP32S3_TWAI);

    object_initialize_child(obj, "i2c0", &s->i2c[0], TYPE_ESP32_I2C);
    object_initialize_child(obj, "i2c1", &s->i2c[1], TYPE_ESP32_I2C);

    object_initialize_child(obj, "sdmmc", &s->sdmmc, TYPE_DWC_SDMMC);
}

static Property esp32s3_soc_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32s3_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = esp32s3_soc_realize;
    device_class_set_props(dc, esp32s3_soc_properties);
}

static const TypeInfo esp32s3_soc_info = {
    .name = TYPE_ESP32S3_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(Esp32s3SocState),
    .instance_init = esp32s3_soc_init,
    .class_init = esp32s3_soc_class_init
};

static void esp32s3_soc_register_types(void)
{
    type_register_static(&esp32s3_soc_info);
}

type_init(esp32s3_soc_register_types)


static uint64_t translate_phys_addr(void *opaque, uint64_t addr)
{
    XtensaCPU *cpu = opaque;

    return cpu_get_phys_page_debug(CPU(cpu), addr);
}

OBJECT_DECLARE_SIMPLE_TYPE(Esp32s3MachineState, ESP32S3_MACHINE)

// -----------------------------------------------

static void esp32s3_soc_add_unimp_device(MemoryRegion *dest, const char* name, hwaddr dport_base_addr, size_t size)
{
    create_unimplemented_device(name, dport_base_addr, size);
    char * name_apb = g_strdup_printf("%s-apb", name);
    create_unimplemented_device(name_apb, dport_base_addr + APB_REG_BASE, size);
    g_free(name_apb);
}

static void esp32s3_machine_init(MachineState *machine)
{
    DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);
    BlockBackend* blk = NULL;
    if (dinfo) {
        /* MTD was given! We need to initialize and emulate SPI flash */
        qemu_log("Adding SPI flash device\n");
        blk = blk_by_legacy_dinfo(dinfo);
    } else {
        qemu_log("Not initializing SPI Flash\n");
    }

    MemoryRegion *sys_mem = get_system_memory();
    Esp32s3MachineState *ms = ESP32S3_MACHINE(machine);
    object_initialize_child(OBJECT(ms), "soc", &ms->esp32s3, TYPE_ESP32S3_SOC);
    Esp32s3SocState *ss = ESP32S3_SOC(&ms->esp32s3);

    MemoryRegion *dram = g_new(MemoryRegion, 1);
    const struct MemmapEntry *memmap = esp32s3_memmap;

    memory_region_init_ram(dram, NULL, "esp32s3.dram",
                           memmap[ESP32S3_MEMREGION_DRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32S3_MEMREGION_DRAM].base, dram);


    memory_region_init_io(&ss->iomem, OBJECT(&ss->cpu[0]), &esp32s3_io_ops,
                          NULL, "esp32s3.iomem", 0xd1000);
    memory_region_add_subregion(sys_mem, ESP32S3_IO_START_ADDR, &ss->iomem);

    // qdev_prop_set_chr(DEVICE(ss), "serial0", serial_hd(0));
    // qdev_prop_set_chr(DEVICE(ss), "serial1", serial_hd(1));
    // qdev_prop_set_chr(DEVICE(ss), "serial2", serial_hd(2));

    qdev_realize(DEVICE(ss), NULL, &error_fatal);

    object_initialize_child(OBJECT(ss), "extmem", &ss->cache, TYPE_ESP32S3_CACHE);
    object_initialize_child(OBJECT(ss), "spi1", &ss->spi1, TYPE_ESP32S3_SPI);
    object_initialize_child(OBJECT(ss), "efuse", &ss->efuse, TYPE_ESP32S3_EFUSE);
    object_initialize_child(OBJECT(ss), "jtag", &ss->jtag, TYPE_ESP32C3_JTAG);
    object_initialize_child(OBJECT(ss), "gpio", &ss->gpio, TYPE_ESP32S3_GPIO);
    object_initialize_child(OBJECT(ss), "rng", &ss->rng, TYPE_ESP32S3_RNG);

    object_initialize_child(OBJECT(ss), "clock", &ss->clock, TYPE_ESP32S3_CLOCK);

    object_initialize_child(OBJECT(ss), "gdma", &ss->gdma, TYPE_ESP32S3_GDMA);
    object_initialize_child(OBJECT(ss), "sha", &ss->sha, TYPE_ESP32S3_SHA);
    object_initialize_child(OBJECT(ss), "aes", &ss->aes, TYPE_ESP32S3_AES);
    object_initialize_child(OBJECT(ss), "rsa", &ss->rsa, TYPE_ESP32S3_RSA);
    object_initialize_child(OBJECT(ss), "hmac", &ss->hmac, TYPE_ESP32S3_HMAC);
    object_initialize_child(OBJECT(ss), "ds", &ss->ds, TYPE_ESP32S3_DS);
    object_initialize_child(OBJECT(ss), "pms", &ss->pms, TYPE_ESP32S3_PMS);

    object_initialize_child(OBJECT(ss), "xts_aes", &ss->xts_aes, TYPE_ESP32S3_XTS_AES);
    object_initialize_child(OBJECT(ss), "timg0", &ss->timg[0], TYPE_ESP32S3_TIMG);
    object_initialize_child(OBJECT(ss), "timg1", &ss->timg[1], TYPE_ESP32S3_TIMG);
    object_initialize_child(OBJECT(ss), "systimer", &ss->systimer, TYPE_ESP32S3_SYSTIMER);
    object_initialize_child(OBJECT(ss), "rgb", &ss->rgb, TYPE_ESP_RGB);

    DeviceState* intmatrix_dev = DEVICE(&ss->intmatrix);
    {
        /* Store the current Machine CPU in the interrupt matrix */
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->intmatrix), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_INTERRUPT_BASE, mr, 0);
    }

    /* Initialize OpenCores Ethernet controller now sicne it requires the interrupt matrix */
    esp32s3_init_openeth(ss);

    /* USB Serial JTAG realization.
     * TBD twin: back it with serial_hd(2) so the firmware's USB-CDC console
     * (Arduino ARDUINO_USB_MODE=1 -> HWCDC) is captured on a 3rd -serial instead
     * of being dropped. Falls back to unbacked (TX discarded) if no chardev. */
    {
        Chardev *cdc = serial_hd(2);
        if (cdc) {
            qdev_prop_set_chr(DEVICE(&ss->jtag), "chardev", cdc);
        }
        sysbus_realize(SYS_BUS_DEVICE(&ss->jtag), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->jtag), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_USB_SERIAL_JTAG_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ss->jtag), 0,
                           qdev_get_gpio_in(intmatrix_dev, ETS_USB_SERIAL_JTAG_INTR_SOURCE));
    }

    /* SPI1 controller (SPI Flash) */
    {
        ss->spi1.xts_aes = &ss->xts_aes;
        sysbus_realize(SYS_BUS_DEVICE(&ss->spi1), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->spi1), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SPI1_BASE, mr, 0);
        if (blk) {
            esp32s3_init_spi_flash(ss, blk);
        }
        if (machine->ram_size > 0) {
            esp32s3_machine_init_psram(ss, (uint32_t) (machine->ram_size / MiB));
        }
    }

    /* (Extmem) Cache realization */
    {
        if (blk) {
            ss->cache.flash_blk = blk;
        }
        if (ss->psram) {
            ss->cache.psram = ss->psram;
        }
        ss->cache.xts_aes = &ss->xts_aes;
        sysbus_realize(SYS_BUS_DEVICE(&ss->cache), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->cache), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_EXTMEM_BASE, mr, 0);

        memory_region_add_subregion(sys_mem, memmap[ESP32S3_MEMREGION_DCACHE].base, &ss->cache.dcache);
        memory_region_add_subregion(sys_mem, memmap[ESP32S3_MEMREGION_ICACHE].base, &ss->cache.icache);
    }

    /* eFuses realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->efuse), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->efuse), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_EFUSE_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ss->efuse), 0,
                       qdev_get_gpio_in(intmatrix_dev, ETS_EFUSE_INTR_SOURCE));
    }

    /* System clock realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->clock), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->clock), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSTEM_BASE, mr, 0);
        for (int i = 0; i < 2; i++) {
            qdev_connect_gpio_out_named(DEVICE(&ss->clock), "i2c-reset", i,
                qdev_get_gpio_in_named(DEVICE(&ss->i2c[i]),
                                      "peripheral-reset", 0));
        }
        /* Connect the IRQ lines to the interrupt matrix */
        for (int i = 0; i < ESP32S3_SYSTEM_CPU_INTR_COUNT; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&ss->clock), i,
                           qdev_get_gpio_in(intmatrix_dev, ETS_FROM_CPU_INTR0_SOURCE + i));
        }
    }
    /* Timer Groups realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->timg[0]), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->timg[0]), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_TIMERGROUP0_BASE, mr, 0);
        /* Connect the T0 interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ss->timg[0]), ESP32S3_T0_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG0_T0_LEVEL_INTR_SOURCE));
        /* Connect the T1 interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ss->timg[0]), ESP32S3_T1_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG0_T1_LEVEL_INTR_SOURCE));
        /* Connect the Watchdog interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ss->timg[0]), ESP32S3_WDT_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG0_WDT_LEVEL_INTR_SOURCE));
    }
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->timg[1]), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->timg[1]), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_TIMERGROUP1_BASE, mr, 0);
        /* Connect the T0 interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ss->timg[1]), ESP32S3_T0_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG1_T0_LEVEL_INTR_SOURCE));
        /* Connect the T1 interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ss->timg[1]), ESP32S3_T1_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG1_T1_LEVEL_INTR_SOURCE));
        /* Connect the Watchdog interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ss->timg[1]), ESP32S3_WDT_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG1_WDT_LEVEL_INTR_SOURCE));
    }

    /* System timer */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->systimer), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->systimer), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSTIMER_BASE, mr, 0);
        for (int i = 0; i < ESP_SYSTIMER_IRQ_COUNT; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&ss->systimer), i,
                           qdev_get_gpio_in(intmatrix_dev, ETS_SYSTIMER_TARGET0_EDGE_INTR_SOURCE + i));
        }
    }


    /* GPIO realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->gpio), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->gpio), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_GPIO_BASE, mr, 0);
        /* TBD twin: attach the Grove HX711 load cell (bit-bang on GPIO4/5). */
        tbd_hx711_init(&ss->gpio.parent);
    }

    {
        qdev_realize(DEVICE(&ss->rng), &ss->periph_bus, &error_fatal);
        esp32s3_soc_add_periph_device(sys_mem, &ss->rng, ESP32S3_RNG_BASE);

    }


    /* GDMA Realization */
    {
        object_property_set_link(OBJECT(&ss->gdma), "soc_mr", OBJECT(dram), &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&ss->gdma), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->gdma), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_GDMA_BASE, mr, 0);
        /* Connect the IRQs to the Interrupt Matrix */
        for (int i = 0; i < ESP32S3_GDMA_CHANNEL_COUNT; i++) {
            qdev_connect_gpio_out_named(DEVICE(&ss->gdma), ESP_GDMA_IRQ_IN_NAME, i,
                                        qdev_get_gpio_in(intmatrix_dev, ETS_DMA_IN_CH0_INTR_SOURCE + i));
            qdev_connect_gpio_out_named(DEVICE(&ss->gdma), ESP_GDMA_IRQ_OUT_NAME, i,
                                        qdev_get_gpio_in(intmatrix_dev, ETS_DMA_OUT_CH0_INTR_SOURCE + i));
        }
   }

    /* SHA realization */
    {
        ss->sha.parent.gdma = ESP_GDMA(&ss->gdma);
        sysbus_realize(SYS_BUS_DEVICE(&ss->sha), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->sha), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SHA_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ss->sha), 0,
                           qdev_get_gpio_in(intmatrix_dev, ETS_SHA_INTR_SOURCE));
    }

    /* AES realization */
    {
        ss->aes.parent.gdma = ESP_GDMA(&ss->gdma);
        sysbus_realize(SYS_BUS_DEVICE(&ss->aes), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->aes), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_AES_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ss->aes), 0,
                           qdev_get_gpio_in(intmatrix_dev, ETS_AES_INTR_SOURCE));
    }
    /* RSA realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->rsa), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->rsa), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_RSA_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ss->rsa), 0,
                           qdev_get_gpio_in(intmatrix_dev, ETS_RSA_INTR_SOURCE));
    }
    /* PMS realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->pms), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->pms), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SENSITIVE_BASE, mr, 0);
    }

    /* HMAC realization */
    {
        ss->hmac.parent.efuse = ESP_EFUSE(&ss->efuse);
        qdev_realize(DEVICE(&ss->hmac), &ss->periph_bus, &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->hmac), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_HMAC_BASE, mr, 0);
    }


    /* Digital Signature realization */
    {
        ss->ds.parent.hmac = ESP_HMAC(&ss->hmac);
        ss->ds.parent.aes = ESP_AES(&ss->aes);
        ss->ds.parent.rsa = ESP_RSA(&ss->rsa);
        ss->ds.parent.sha = ESP_SHA(&ss->sha);
        qdev_realize(DEVICE(&ss->ds), &ss->periph_bus, &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->ds), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_DIGITAL_SIGNATURE_BASE, mr, 0);
    }
    /* XTS-AES realization */
    {
        ss->xts_aes.efuse = ESP_EFUSE(&ss->efuse);
        ss->xts_aes.clock = &ss->clock;
        qdev_realize(DEVICE(&ss->xts_aes), &ss->periph_bus, &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->xts_aes), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_AES_XTS_BASE, mr, 0);
    }

    /* RGB display realization */
    {
        /* Give the internal RAM memory region to the display */
        ss->rgb.intram = dram;
        sysbus_realize(SYS_BUS_DEVICE(&ss->rgb), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->rgb), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_FRAMEBUF_BASE, mr, 0);
        memory_region_add_subregion_overlap(sys_mem, esp32s3_memmap[ESP32S3_MEMREGION_FRAMEBUF].base, &ss->rgb.vram, 0);
    }

    esp32s3_soc_add_unimp_device(sys_mem, "esp32s3.rmt", DR_REG_RMT_BASE, 0x1000);
    esp32s3_soc_add_unimp_device(sys_mem, "esp32s3.iomux", DR_REG_IO_MUX_BASE, 0x2000);

    
    esp32s3_machine_init_sd(ss);

    /* Need MMU initialized prior to ELF loading,
     * so that ELF gets loaded into virtual addresses
     */
    cpu_reset(CPU(&ss->cpu[0]));

    const char *load_elf_filename = NULL;
    if (machine->firmware) {
        load_elf_filename = machine->firmware;
    }
    if (machine->kernel_filename) {
        qemu_log("Warning: both -bios and -kernel arguments specified. Only loading the the -kernel file.\n");
        load_elf_filename = machine->kernel_filename;
    }

    if (load_elf_filename) {
        uint64_t elf_entry;
        uint64_t elf_lowaddr;
        int size = load_elf(load_elf_filename, NULL,
                               translate_phys_addr, &ss->cpu[0],
                               &elf_entry, &elf_lowaddr,
                               NULL, NULL, 0, EM_XTENSA, 0, 0);
        if (size < 0) {
            error_report("Error: could not load ELF file '%s'", load_elf_filename);
            exit(1);
        }

        if (elf_entry != XCHAL_RESET_VECTOR_PADDR) {
            // Since ROM is empty when loading elf file AND
            // PC value is 0x40000400 after reset
            // need to jump to elf entry point to run a programm
            uint8_t p[4];
            memcpy(p, &elf_entry, 4);
            uint8_t boot[] = {
                0x06, 0x01, 0x00,       /* j    1 */
                0x00,                   /* .literal_position */
                p[0], p[1], p[2], p[3], /* .literal elf_entry */
                                        /* 1: */
                0x01, 0xff, 0xff,       /* l32r a0, elf_entry */
                0xa0, 0x00, 0x00,       /* jx   a0 */
            };
            // Write boot function to reset-vector address (0x40000400) of the CPU 0
            rom_add_blob_fixed_as("boot", boot, sizeof(boot), XCHAL_RESET_VECTOR_PADDR, CPU(&ss->cpu[0])->as);
            ss->cpu[0].env.pc = XCHAL_RESET_VECTOR_PADDR;
        }
    } else {
        char *rom_binary = qemu_find_file(QEMU_FILE_TYPE_BIOS, "esp32s3_rev0_rom.bin");
        if (rom_binary == NULL) {
            error_report("Error: -bios argument not set, and ROM code binary not found (1)");
            exit(1);
        }

        int size = load_image_targphys_as(rom_binary, esp32s3_memmap[ESP32S3_MEMREGION_IROM].base, esp32s3_memmap[ESP32S3_MEMREGION_IROM].size, CPU(&ss->cpu[0])->as);
        if (size < 0) {
            error_report("Error: could not load ROM binary '%s'", rom_binary);
            exit(1);
        }
        g_free(rom_binary);

        if (ESP32S3_CPU_COUNT > 1)
        {
            rom_binary = qemu_find_file(QEMU_FILE_TYPE_BIOS, "esp32s3_rev0_rom.bin");
            if (rom_binary == NULL) {
                error_report("Error: -bios argument not set, and ROM code binary not found (2)");
                exit(1);
            }

            size = load_image_targphys_as(rom_binary, esp32s3_memmap[ESP32S3_MEMREGION_IROM].base, esp32s3_memmap[ESP32S3_MEMREGION_IROM].size, CPU(&ss->cpu[1])->as);
            if (size < 0) {
                error_report("Error: could not load ROM binary '%s'", rom_binary);
                exit(1);
            }
            g_free(rom_binary);
        }
    }
}


static ram_addr_t esp32s3_fixup_ram_size(ram_addr_t requested_size)
{
    ram_addr_t size;
    if (requested_size == 0) {
        size = 0;
    } else if (requested_size <= 2 * MiB) {
        size = 2 * MiB;
    } else if (requested_size <= 4 * MiB ) {
        size = 4 * MiB;
    } else if (requested_size <= 8 * MiB ) {
        size = 8 * MiB;
    } else if (requested_size <= 16 * MiB ) {
        size = 16 * MiB;
    } else if (requested_size <= 32 * MiB ) {
        size = 32 * MiB;
    } else {
        qemu_log("RAM size larger than 32 MB not supported\n");
        size = 32 * MiB;
    }
    return size;
}

/* Initialize machine type */
static void esp32s3_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Espressif ESP32S3 machine";
    mc->init = esp32s3_machine_init;
    mc->max_cpus = 2;
    mc->default_cpus = 2;
    mc->default_ram_size = 0;
    mc->fixup_ram_size = esp32s3_fixup_ram_size;
}

static const TypeInfo esp32s3_info = {
    .name = TYPE_ESP32S3_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(Esp32s3MachineState),
    .class_init = esp32s3_machine_class_init,
};

static void esp32s3_machine_type_init(void)
{
    type_register_static(&esp32s3_info);
}

type_init(esp32s3_machine_type_init);
