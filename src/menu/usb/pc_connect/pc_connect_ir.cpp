#include "pc_connect.h"

#if !defined(LITE_VERSION)
#include "menu/infrared/custom_ir.h"
#include "menu/infrared/ir_utils.h"
#include <ArduinoJson.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include <globals.h>

namespace {

bool g_active = false;
bool g_raw = false;
IRrecv *g_recv = nullptr;
decode_results g_results;

// Flipper-style little-endian hex (matches IrRead::uint32ToString).
String irU32Hex(uint32_t value) {
    char buf[12];
    snprintf(
        buf,
        sizeof(buf),
        "%02X %02X %02X %02X",
        (unsigned)(value & 0xFF),
        (unsigned)((value >> 8) & 0xFF),
        (unsigned)((value >> 16) & 0xFF),
        (unsigned)((value >> 24) & 0xFF)
    );
    return String(buf);
}

String protocolName(const decode_results &r) {
    switch (r.decode_type) {
        case decode_type_t::RC5: return (r.command > 0x3F) ? "RC5X" : "RC5";
        case decode_type_t::RC6: return "RC6";
        case decode_type_t::SAMSUNG: return "Samsung32";
        case decode_type_t::SONY:
            if (r.address > 0xFF) return "SIRC20";
            if (r.address > 0x1F) return "SIRC15";
            return "SIRC";
        case decode_type_t::NEC: return (r.address > 0xFF) ? "NECext" : "NEC";
        default: return typeToString(r.decode_type, r.repeat);
    }
}

String rawDataString(const decode_results &r) {
    uint16_t *rawcode = resultToRawArray(&r);
    uint16_t len = getCorrectedRawLength(&r);
    String signal;
    for (uint16_t i = 0; i < len; i++) {
        signal += String(rawcode[i]);
        if (i + 1 < len) signal += " ";
    }
    delete[] rawcode;
    return signal;
}

} // namespace

bool pcConnectIrRxActive() { return g_active; }

bool pcConnectIrRxStart(bool raw) {
    pcConnectStopRadio();
    if (kvxConfigPins.irRx < 0) return false;
    g_raw = raw;
    setup_ir_pin(kvxConfigPins.irRx, INPUT);
    g_recv = new IRrecv(kvxConfigPins.irRx, SAFE_STACK_BUFFER_SIZE / 2, 50);
    if (!g_recv) return false;
    g_recv->enableIRIn();
    g_active = true;
    pcConnectSetStatus(PcRadio::Ir, raw ? "ir.rx.raw" : "ir.rx", 0);
    return true;
}

void pcConnectIrRxStop() {
    if (!g_active) return;
    if (g_recv) {
        g_recv->disableIRIn();
        delete g_recv;
        g_recv = nullptr;
    }
    g_active = false;
}

void pcConnectIrRxTick() {
    if (!g_active || !g_recv) return;
    if (!g_recv->decode(&g_results)) return;

    JsonDocument doc;
    doc["evt"] = "ir";
    if (g_raw || g_results.decode_type == decode_type_t::UNKNOWN) {
        doc["raw"] = true;
        doc["freq"] = 38000;
        String data = rawDataString(g_results);
        // Keep USB lines under buffer limit.
        if (data.length() > 1500) data = data.substring(0, 1500);
        doc["data"] = data;
    } else {
        doc["raw"] = false;
        doc["protocol"] = protocolName(g_results);
        doc["address"] = irU32Hex((uint32_t)g_results.address);
        doc["command"] = irU32Hex((uint32_t)g_results.command);
        doc["bits"] = (int)g_results.bits;
    }
    String out;
    serializeJson(doc, out);
    pcConnectEmitJson(out);
    g_recv->resume();
}

bool pcConnectIrTx(const String &protocol, const String &address, const String &command) {
    if (pcConnectRadio() != PcRadio::Idle && pcConnectRadio() != PcRadio::Ir) {
        // Allow TX while idle; stop RX first if needed.
    }
    bool wasRx = g_active;
    if (wasRx) pcConnectIrRxStop();
    else if (pcConnectRadio() != PcRadio::Idle) pcConnectStopRadio();

    IRCode code;
    code.type = "parsed";
    code.protocol = protocol;
    code.address = address;
    code.command = command;
    sendIRCommand(&code, true);

    if (wasRx) pcConnectIrRxStart(g_raw);
    return true;
}

bool pcConnectIrTxRaw(uint32_t freq, const String &samples) {
    bool wasRx = g_active;
    bool wasRaw = g_raw;
    if (wasRx) pcConnectIrRxStop();
    else if (pcConnectRadio() != PcRadio::Idle) pcConnectStopRadio();

    IRCode code;
    code.type = "raw";
    code.frequency = (uint16_t)freq;
    code.data = samples;
    sendIRCommand(&code, true);

    if (wasRx) pcConnectIrRxStart(wasRaw);
    return true;
}

#endif
