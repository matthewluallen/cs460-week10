#include "console_health.h"
#include "acquisition_record.h"
#include <ArduinoJson.h>
#include <esp_timer.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1
#include <soc/usb_serial_jtag_struct.h>
#endif

namespace {
portMUX_TYPE mutex=portMUX_INITIALIZER_UNLOCKED;
struct Counts {uint32_t polls=0,rx_bytes=0,commands=0;bool saturated=false;} counts;
void increment(uint32_t& value) {
    portENTER_CRITICAL(&mutex);
    if(value==UINT32_MAX)counts.saturated=true;else ++value;
    portEXIT_CRITICAL(&mutex);
}
}
void console_health_poll(){increment(counts.polls);}
void console_health_rx(){increment(counts.rx_bytes);}
void console_health_command(){increment(counts.commands);}
String console_health_json() {
    Counts copy;
    portENTER_CRITICAL(&mutex);copy=counts;portEXIT_CRITICAL(&mutex);
    StaticJsonDocument<1536> doc;
    doc["schema"]="tbd.console-health/v1";
    doc["boot_id"]=acquisition_record_boot_id();
    char elf[65];const auto* app=esp_app_get_description();
    for(unsigned i=0;i<32;++i)snprintf(elf+2*i,3,"%02x",app->app_elf_sha256[i]);
    doc["elf_sha256"]=elf;doc["captured_us"]=uint64_t(esp_timer_get_time());
    doc["clock"]="esp_timer_boot_us";
    doc["console_polls"]=copy.polls;doc["text_rx_bytes"]=copy.rx_bytes;
    doc["dispatched_lines"]=copy.commands;doc["saturated"]=copy.saturated;
    // Queue accessors do not consume bytes. Do not call bool(Serial), flush(),
    // or isConnected(): HWCDC's connection check changes the FIFO/interrupts.
    doc["rx_queue_bytes"]=Serial.available();
    doc["tx_queue_free"]=Serial.availableForWrite();
    auto usb=doc.createNestedObject("usb_registers");
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1
    // Read-only, sequential hardware samples, not an atomic peripheral image.
    usb["supported"]=true;usb["int_raw"]=USB_SERIAL_JTAG.int_raw.val;
    usb["int_st"]=USB_SERIAL_JTAG.int_st.val;usb["int_ena"]=USB_SERIAL_JTAG.int_ena.val;
    usb["ep1_conf"]=USB_SERIAL_JTAG.ep1_conf.val;usb["conf0"]=USB_SERIAL_JTAG.conf0.val;
#else
    usb["supported"]=false;
#endif
    if(doc.overflowed())return String("{\"error\":\"console health capacity\"}");
    String result;serializeJson(doc,result);return result;
}
