#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/i2c/esp32_i2c.h"
#include "hw/irq.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h"
#include "trace.h"

/* Host-owned transaction observations, not guest diagnostics or wire edges.
 * Sequence remains monotonic through guest resets. The capture manifest must
 * bind the canonical QOM controller path to its machine/session and bus.
 * QEMU virtual time is model execution time, not an SCL timing measurement.
 */
static void esp32_i2c_observe(Esp32I2CState *s, const char *event,
                              int data, int nack)
{
    char *path = object_get_canonical_path(OBJECT(s));
    trace_esp32_i2c_observe(s, path ? path : "unresolved",
                          ++s->observe_seq, s->observe_epoch,
                          qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), event,
                          s->observe_address, s->observe_read, data, nack);
    g_free(path);
}

static void esp32_i2c_do_transaction(Esp32I2CState * s);
static void esp32_i2c_update_irq(Esp32I2CState * s);

static void esp32_i2c_reset_hold(Object *obj, ResetType type)
{
    Esp32I2CState * s = Esp32_I2C(obj);

    s->peripheral_reset = false;
    s->observe_epoch++;
    esp32_i2c_observe(s, "reset", -1, -1);
    s->observe_address = s->observe_read = -1;
    if (s->trans_ongoing || i2c_bus_busy(s->bus)) {
        i2c_end_transfer(s->bus);
    }
    fifo8_reset(&s->rx_fifo);
    fifo8_reset(&s->tx_fifo);
    s->trans_ongoing = false;
    s->read_pending=false;s->read_remaining=0;s->resume_cmd=0;s->rx_watermark=12;
    s->write_pending=false;s->write_remaining=0;
    s->fifo_conf_reg=11|(4<<5)|(s->s3_mode ? (1<<14) : 0);
    s->ctr_reg = 0;
    s->timeout_reg = 0;
    s->int_ena_reg = 0;
    s->int_raw_reg = 0;
    s->sda_hold_reg = 0;
    s->sda_sample_reg = 0;
    s->high_period_reg = 0;
    s->low_period_reg = 0;
    s->start_hold_reg = 0;
    s->rstart_setup_reg = 0;
    s->stop_hold_reg = 0;
    s->stop_setup_reg = 0;
    memset(s->cmd_reg, 0, sizeof(s->cmd_reg));

    fifo8_reset(&s->tx_fifo);
    fifo8_reset(&s->rx_fifo);
    esp32_i2c_update_irq(s);
}

/* A level-held peripheral reset, independent of slave-device reset. Ending
 * the selected bus transaction releases its routing but does not reset sensors.
 * Keep this local to the controller: QEMU reset-tree propagation would also reset
 * child I2C slaves, which are not reset by the SoC peripheral-reset register. */
static void esp32_i2c_peripheral_reset(void *opaque, int n, int level)
{
    Esp32I2CState *s = Esp32_I2C(opaque);
    bool asserted = level != 0;
    if (asserted && !s->peripheral_reset) {
        esp32_i2c_reset_hold(OBJECT(s), RESET_TYPE_COLD);
    }
    s->peripheral_reset = asserted;
}

static uint32_t esp32_i2c_get_status_reg(Esp32I2CState* s)
{
    uint32_t res = 0;
    res = FIELD_DP32(res, I2C_STATUS, BUS_BUSY, s->trans_ongoing || i2c_bus_busy(s->bus));
    res = FIELD_DP32(res, I2C_STATUS, RXFIFO_CNT, fifo8_num_used(&s->rx_fifo));
    res = FIELD_DP32(res, I2C_STATUS, TXFIFO_CNT, fifo8_num_used(&s->tx_fifo));
    return res;
}

static void esp32_i2c_update_irq(Esp32I2CState * s)
{
    int irq_state = !!(s->int_raw_reg & s->int_ena_reg);
    qemu_set_irq(s->irq, irq_state);
}

static uint64_t esp32_i2c_read(void * opaque, hwaddr addr, unsigned int size)
{
    Esp32I2CState * s = Esp32_I2C(opaque);

    if (s->peripheral_reset) {
        return 0;
    }

    switch(addr) {
    case A_I2C_CTR:
        return s->ctr_reg;
    case A_I2C_STATUS:
        return esp32_i2c_get_status_reg(s);
    case A_I2C_FIFO_CONF:
        return s->fifo_conf_reg;
    case A_I2C_FIFO_DATA: {
        if (fifo8_num_used(&s->rx_fifo) == 0) {
            error_report("esp32_i2c: read I2C FIFO while it is empty");
            return 0xee;
        }
        uint8_t res = fifo8_pop(&s->rx_fifo);
        // Resume the paused wire transaction after the guest drains this
        // watermark batch. Do not discard bytes beyond the 32-byte FIFO.
        if(s->read_pending && fifo8_num_used(&s->rx_fifo)==0)
            esp32_i2c_do_transaction(s);
        return res;
    }
    case A_I2C_INT_RAW:
        return s->int_raw_reg;
    case A_I2C_INT_ENA:
        return s->int_ena_reg;
    case A_I2C_INT_ST:
        return s->int_raw_reg & s->int_ena_reg;
    case A_I2C_CMD ... (A_I2C_CMD + (ESP32_I2C_CMD_COUNT - 1) * 4):
        if (s->s3_mode && addr >= A_I2C_CMD + 8 * 4) {
            return 0;
        }
        return s->cmd_reg[(addr - A_I2C_CMD) / 4];
    case A_I2C_TIMEOUT:
        return s->timeout_reg;
    case A_I2C_SDA_HOLD:
        return s->sda_hold_reg;
    case A_I2C_SDA_SAMPLE:
        return s->sda_sample_reg;
    case A_I2C_HIGH_PERIOD:
        return s->high_period_reg;
    case A_I2C_LOW_PERIOD:
        return s->low_period_reg;
    case A_I2C_START_HOLD:
        return s->start_hold_reg;
    case A_I2C_RSTART_SETUP:
        return s->rstart_setup_reg;
    case A_I2C_STOP_HOLD:
        return s->stop_hold_reg;
    case A_I2C_STOP_SETUP:
        return s->stop_setup_reg;
    default:
        return 0;
    }
}

static void esp32_i2c_write(void * opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    Esp32I2CState * s = Esp32_I2C(opaque);

    if (s->peripheral_reset) {
        return;
    }

    switch(addr) {
    case A_I2C_CTR:
        if (s->s3_mode && (value & (1 << 10))) { /* S3 SCL FSM reset */
            s->observe_epoch++;
            esp32_i2c_observe(s, "fsm_reset", -1, -1);
            s->observe_address = s->observe_read = -1;
            if (s->trans_ongoing || i2c_bus_busy(s->bus)) {
                i2c_end_transfer(s->bus);
            }
            s->trans_ongoing = false;
            s->read_pending = s->write_pending = false;
            s->read_remaining = s->write_remaining = 0;
            s->resume_cmd = 0;
            value &= ~(1 << 10);
        }
        if (FIELD_EX32(value, I2C_CTR, MS_MODE) != 1) {
            error_report("esp32_i2c: slave mode not implemented");
        }
        if (FIELD_EX32(value, I2C_CTR, TRANS_START)) {
            s->resume_cmd=0;s->read_pending=false;s->read_remaining=0;
            s->write_pending=false;s->write_remaining=0;
            esp32_i2c_do_transaction(s);
            value &= ~ R_I2C_CTR_TRANS_START_MASK;
        }
        s->ctr_reg = value;
        break;
    case A_I2C_FIFO_CONF:
        s->rx_watermark=(value & 31)+1;
        s->fifo_conf_reg=value & ~(R_I2C_FIFO_CONF_RX_FIFO_RST_MASK | R_I2C_FIFO_CONF_TX_FIFO_RST_MASK);
        if (FIELD_EX32(value, I2C_FIFO_CONF, NONFIFO_EN)) {
            error_report("esp32_i2c: APB mode not implemented");
        }
        if (FIELD_EX32(value, I2C_FIFO_CONF, RX_FIFO_RST)) {
            fifo8_reset(&s->rx_fifo);s->read_pending=false;s->read_remaining=0;
        }
        if (FIELD_EX32(value, I2C_FIFO_CONF, TX_FIFO_RST)) {
            fifo8_reset(&s->tx_fifo);
        }
        break;
    case A_I2C_FIFO_DATA:
        if (fifo8_num_free(&s->tx_fifo) == 0) {
            error_report("esp32_i2c: write to I2C TX FIFO while it is full");
        } else {
            fifo8_push(&s->tx_fifo, value);
            if (s->write_pending) {
                esp32_i2c_do_transaction(s);
            }
        }
        break;
    case A_I2C_INT_CLR:
        s->int_raw_reg &= ~value;
        esp32_i2c_update_irq(s);
        break;
    case A_I2C_INT_ENA:
        s->int_ena_reg = value;
        esp32_i2c_update_irq(s);
        break;
    case A_I2C_CMD ... (A_I2C_CMD + (ESP32_I2C_CMD_COUNT - 1) * 4):
        if (!s->s3_mode || addr < A_I2C_CMD + 8 * 4) {
            s->cmd_reg[(addr - A_I2C_CMD) / 4] = value;
        }
        break;
    case A_I2C_TIMEOUT:
        s->timeout_reg = value;
        break;
    case A_I2C_SDA_HOLD:
        s->sda_hold_reg = value;
        break;
    case A_I2C_SDA_SAMPLE:
        s->sda_sample_reg = value;
        break;
    case A_I2C_HIGH_PERIOD:
        s->high_period_reg = value;
        break;
    case A_I2C_LOW_PERIOD:
        s->low_period_reg = value;
        break;
    case A_I2C_START_HOLD:
        s->start_hold_reg = value;
        break;
    case A_I2C_RSTART_SETUP:
        s->rstart_setup_reg = value;
        break;
    case A_I2C_STOP_HOLD:
        s->stop_hold_reg = value;
        break;
    case A_I2C_STOP_SETUP:
        s->stop_setup_reg = value;
        break;
    default:
        break;
    }
}

static void esp32_i2c_ack_failure(Esp32I2CState *s)
{
    /* S3 TRM 27.4.9: checked ACK mismatch raises NACK and generates STOP.
     * STOP detection also latches TRANS_COMPLETE; the guest checks NACK first.
     * The incomplete WRITE and any following command must not become DONE.
     */
    esp32_i2c_observe(s, "ack_mismatch_stop", -1, -1);
    s->int_raw_reg |= R_I2C_INT_RAW_ACK_ERR_MASK |
                      R_I2C_INT_RAW_TRANS_COMPLETE_MASK;
    i2c_end_transfer(s->bus);
    s->trans_ongoing = false;
    s->read_pending = false;
    s->read_remaining = 0;
    s->write_pending = false;
    s->write_remaining = 0;
    s->resume_cmd = 0;
    esp32_i2c_update_irq(s);
}

static void esp32_i2c_do_transaction(Esp32I2CState * s)
{
    bool stop_or_end = false;
    int command_count = s->s3_mode ? 8 : ESP32_I2C_CMD_COUNT;
    for (int i_cmd = s->resume_cmd; i_cmd < command_count && !stop_or_end; ++i_cmd) {
        uint32_t cmd = s->cmd_reg[i_cmd];
        char opcode = FIELD_EX32(cmd, I2C_CMD, OPCODE);
        if (s->s3_mode) {
            /* ESP32-S3/S2/C3 command opcodes differ from the original ESP32:
             * RSTART 6->0, READ 3->2, STOP 2->3 (WRITE 1 and END 4 unchanged).
             * Remap to the model's original-ESP32 numbering. */
            switch (opcode) {
                case 6: opcode = I2C_OPCODE_RSTART; break;
                case 3: opcode = I2C_OPCODE_READ;   break;
                case 2: opcode = I2C_OPCODE_STOP;   break;
                default: break;
            }
        }
        switch (opcode) {
            case I2C_OPCODE_RSTART:
                esp32_i2c_observe(s, "rstart_command", -1, -1);
                s->observe_address = s->observe_read = -1;
                /* Preserve the slave transaction across repeated START. QEMU's
                 * i2c_start_transfer delivers the new direction without FINISH;
                 * an artificial STOP here discards SMBus command state. The
                 * next WRITE still begins with an address byte. */
                s->trans_ongoing = false;
                break;
            case I2C_OPCODE_WRITE: {
                bool check_ack = FIELD_EX32(cmd, I2C_CMD, ACK_CHECK_EN);
                bool expected_nack = FIELD_EX32(cmd, I2C_CMD, ACK_EXP);
                if (!s->write_pending) {
                    s->write_remaining = FIELD_EX32(cmd, I2C_CMD, BYTE_NUM);
                    if (!s->write_remaining) {
                        /* TRM permits 1..255. Refuse the unsupported zero count
                         * without popping an address, wrapping size_t, claiming
                         * command completion or inventing a slave NACK.
                         */
                        error_report("esp32_i2c: zero-length WRITE refused");
                        esp32_i2c_observe(s, "invalid_write_count", 0, -1);
                        s->resume_cmd = 0;
                        esp32_i2c_update_irq(s);
                        return;
                    }
                }
                while (s->write_remaining) {
                    if (!fifo8_num_used(&s->tx_fifo)) {
                        /* Suspend at this byte boundary. Guest refill resumes
                         * the same command; no empty pop, zero-byte substitution
                         * or false completion. This is transaction-level pacing,
                         * not a model of SCL hold duration or clock stretch.
                         */
                        esp32_i2c_observe(s, "tx_wait", -1, -1);
                        s->write_pending = true;
                        s->resume_cmd = i_cmd;
                        if (s->s3_mode && (s->fifo_conf_reg & (1 << 14))) {
                            s->int_raw_reg |= 1 << 6; /* master TX underflow */
                            if ((s->fifo_conf_reg >> 5) & 31) {
                                s->int_raw_reg |= 1 << 1; /* TX watermark */
                            }
                        }
                        esp32_i2c_update_irq(s);
                        return;
                    }
                    uint8_t data = fifo8_pop(&s->tx_fifo);
                    s->write_remaining--;
                    bool nack;
                    if (!s->trans_ongoing) {
                        s->trans_ongoing = true;
                        s->observe_address = data >> 1;
                        s->observe_read = data & 1;
                        nack = i2c_start_transfer(s->bus, data >> 1, data & 1) != 0;
                        esp32_i2c_observe(s, "address", data, nack);
                    } else {
                        nack = i2c_send(s->bus, data) != 0;
                        esp32_i2c_observe(s, "write", data, nack);
                    }
                    if (check_ack && nack != expected_nack) {
                        esp32_i2c_ack_failure(s);
                        return;
                    }
                }
                s->write_pending = false;
                /* ACK_ERR stays latched until INT_CLR, including across a
                 * successful later address or command.
                 */
                break;
            }
            case I2C_OPCODE_READ: {
                if(!s->read_pending)s->read_remaining=FIELD_EX32(cmd,I2C_CMD,BYTE_NUM);
                while(s->read_remaining) {
                    if(fifo8_num_used(&s->rx_fifo)>=(s->int_ena_reg&1 ? s->rx_watermark : ESP32_I2C_FIFO_LENGTH)) {
                        esp32_i2c_observe(s, "rx_wait", -1, -1);
                        s->resume_cmd=i_cmd;s->read_pending=true;
                        s->int_raw_reg |= 1; /* ESP32-S3 RXFIFO watermark bit0 */
                        esp32_i2c_update_irq(s);return;
                    }
                    uint8_t data = i2c_recv(s->bus);
                    esp32_i2c_observe(s, "read", data, -1);
                    fifo8_push(&s->rx_fifo,data);--s->read_remaining;
                }
                s->read_pending=false;
                break;
            }
            case I2C_OPCODE_STOP:
                i2c_end_transfer(s->bus);
                esp32_i2c_observe(s, "stop", -1, -1);
                s->observe_address = s->observe_read = -1;
                s->trans_ongoing = false;
                s->int_raw_reg = FIELD_DP32(s->int_raw_reg, I2C_INT_RAW, TRANS_COMPLETE, 1);
                stop_or_end = true;
                break;
            case I2C_OPCODE_END:
                esp32_i2c_observe(s, "end_command", -1, -1);
                s->int_raw_reg = FIELD_DP32(s->int_raw_reg, I2C_INT_RAW, END_DETECT, 1);
                stop_or_end = true;
                break;
            default:
                error_report("esp32_i2c: Invalid command %d opcode %d", i_cmd, opcode);
                esp32_i2c_observe(s, "invalid_opcode", opcode, -1);
                break;
        }
        s->cmd_reg[i_cmd] = FIELD_DP32(s->cmd_reg[i_cmd], I2C_CMD, DONE, 1);
    }
    s->resume_cmd=0;
    esp32_i2c_update_irq(s);
}

static const MemoryRegionOps esp32_i2c_ops = {
    .read = esp32_i2c_read,
    .write = esp32_i2c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* QMP qom-get and these MMIO handlers run under the BQL. One read captures
 * one controller's sequence/epoch/virtual clock without generating a bus event,
 * touching a guest register, or stopping/resuming the VM. Controllers are read
 * separately: callers must not claim an atomic snapshot across controllers.
 */
static char *esp32_i2c_observation_fence(Object *obj, Error **errp)
{
    Esp32I2CState *s = Esp32_I2C(obj);
    assert(bql_locked());
    return g_strdup_printf("v1:%" PRIu64 ":%" PRIu64 ":%" PRId64,
                           s->observe_seq, s->observe_epoch,
                           qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
}

static void esp32_i2c_init(Object * obj)
{
    Esp32I2CState *s = Esp32_I2C(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_i2c_ops, s, TYPE_ESP32_I2C, ESP32_I2C_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), esp32_i2c_peripheral_reset,
                            "peripheral-reset", 1);

    object_property_add_str(obj, "observation-fence",
                            esp32_i2c_observation_fence, NULL);
    s->observe_address = s->observe_read = -1;
    s->bus = i2c_init_bus(DEVICE(s), "i2c");

    fifo8_create(&s->tx_fifo, ESP32_I2C_FIFO_LENGTH);
    fifo8_create(&s->rx_fifo, ESP32_I2C_FIFO_LENGTH);
}

static void esp32_i2c_class_init(ObjectClass * klass, void * data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.hold = esp32_i2c_reset_hold;
}

static const TypeInfo esp32_i2c_type_info = {
    .name = TYPE_ESP32_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32I2CState),
    .instance_init = esp32_i2c_init,
    .class_init = esp32_i2c_class_init,
};

static void esp32_i2c_register_types(void)
{
    type_register_static(&esp32_i2c_type_info);
}

type_init(esp32_i2c_register_types)
