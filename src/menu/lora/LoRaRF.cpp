#if !defined(LITE_VERSION)
#include "root/storage/paths.h"
#include "LoRaRF.h"
#include "WString.h"
#include "root/hal/bus_HAL.h"
#include "root/config/config.h"
#include "root/config/configPins.h"
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <RadioLib.h>
#include <root/ui/display.h>
#include <root/input/mykeyboard.h>
#include <root/app/utils.h>
#include <globals.h>
#include <vector>
#if defined(HAS_LORA_CAP)
#include <Wire.h>
#endif

extern KvxputerConfigPins kvxConfigPins;

bool update = false;
String msg;
String rcvmsg;
String displayName;
bool intlora = true;
// scrolling thing
std::vector<String> messages;
int scrollOffset = 0;
const int maxMessages = 19;

#define spreadingFactor 9
#define SignalBandwidth 31.25E3
#define codingRateDenominator 8
#define preambleLength 8

int contentWidth = tftWidth - 20;
int yStart = 35;
int yPos = yStart;
int ySpacing = 10;
int rightColumnX = tftWidth / 2 + 10;
SPIClass *loraSpi = nullptr;
Module *loraModule = nullptr;
SX1276 *lora1276 = nullptr;
SX1262 *lora1262 = nullptr;
volatile bool loraPacketReceived = false;
volatile bool loraInterruptEnabled = true;
enum class LoRaRadioVariant { SX1276, SX1262 };
#if defined(HAS_LORA_CAP)
LoRaRadioVariant loraRadioVariant = LoRaRadioVariant::SX1262;
#else
LoRaRadioVariant loraRadioVariant = LoRaRadioVariant::SX1276;
#endif

int getLoraIrqPin() {
#ifdef LORA_IRQ
    return LORA_IRQ;
#else
    return kvxConfigPins.LoRa_bus.io2;
#endif
}

int getLoraBusyPin() {
#ifdef LORA_BUSY
    return LORA_BUSY;
#else
    return GPIO_NUM_NC;
#endif
}

int getLoraResetPin() { return kvxConfigPins.LoRa_bus.io0; }
int getLoraCsPin() { return kvxConfigPins.LoRa_bus.cs; }

void clearLoraRadio() {
    if (lora1276) {
        delete lora1276;
        lora1276 = nullptr;
    }
    if (lora1262) {
        delete lora1262;
        lora1262 = nullptr;
    }
    if (loraModule) {
        delete loraModule;
        loraModule = nullptr;
    }
}

void onLoraPacket() {
    if (!loraInterruptEnabled) return;
    loraPacketReceived = true;
}

// Cap LoRa-1262 routes the RP-SMA antenna through FM8625H. P0 on the
// PI4IOE5V6408 (I2C 0x43, SDA G8 / SCL G9) must be high or the radio
// never sees the antenna. https://docs.m5stack.com/en/cap/Cap_LoRa-1262
struct CapAntSwitch {
    bool bus = false;
    bool present = false;
    bool high = false;
};

#if defined(HAS_LORA_CAP)
static constexpr uint8_t kCapIoAddr = 0x43;
static bool loraCapTcxoOn = false;

struct InputPause {
    InputPause() {
        if (xHandle != nullptr) vTaskSuspend(xHandle);
    }
    ~InputPause() {
        if (xHandle != nullptr) vTaskResume(xHandle);
    }
};

static bool ioeReadRaw(uint8_t reg, uint8_t &val) {
    Wire1.beginTransmission(kCapIoAddr);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return false;
    if (Wire1.requestFrom(kCapIoAddr, (uint8_t)1) != 1) return false;
    val = Wire1.read();
    return true;
}

static bool ioeWriteRaw(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(kCapIoAddr);
    Wire1.write(reg);
    Wire1.write(val);
    return Wire1.endTransmission() == 0;
}

static bool ioeUpdateRaw(uint8_t reg, uint8_t mask, bool set) {
    uint8_t cur = 0;
    if (!ioeReadRaw(reg, cur)) return false;
    uint8_t next = set ? (uint8_t)(cur | mask) : (uint8_t)(cur & ~mask);
    if (next == cur) return true;
    return ioeWriteRaw(reg, next);
}

// M5 Cap LoRa-1262 example: 3.0 V TCXO on DIO3 and the LDO regulator.
// Fall back to a plain crystal if the module rejects that clock.
static int16_t beginCapSx1262(float bandMHz) {
    loraCapTcxoOn = true;
    int16_t state = lora1262->begin(
        bandMHz, 125.0, spreadingFactor, codingRateDenominator, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10,
        preambleLength, 3.0f, true
    );
    if (state == RADIOLIB_ERR_SPI_CMD_FAILED || state == RADIOLIB_ERR_SPI_CMD_INVALID ||
        state == RADIOLIB_ERR_SPI_CMD_TIMEOUT || state == RADIOLIB_ERR_INVALID_TCXO_VOLTAGE) {
        Serial.printf("SX1262 TCXO 3.0 V failed (%d), retrying XTAL\n", state);
        loraCapTcxoOn = false;
        state = lora1262->begin(
            bandMHz, 125.0, spreadingFactor, codingRateDenominator, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10,
            preambleLength, 0.0f, false
        );
    }
    if (state == RADIOLIB_ERR_NONE) lora1262->setCurrentLimit(140.0f);
    return state;
}
#endif

static CapAntSwitch loraCapSetAntenna(bool enable) {
    CapAntSwitch sw;
#if defined(HAS_LORA_CAP)
    sw.bus = kvxConfigPins.sys_i2c.sda == (gpio_num_t)8 && kvxConfigPins.sys_i2c.scl == (gpio_num_t)9;
    if (!sw.bus) return sw;
    InputPause pause;
    uint8_t id = 0;
    if (!ioeReadRaw(0x01, id) || id == 0 || (id & 0xF0) != 0xA0) {
        Serial.printf("LoRa Cap: no PI4IOE at 0x43 (id 0x%02X)\n", id);
        return sw;
    }
    sw.present = true;
    // P0 output, out of high-Z, then level. High connects the SMA antenna.
    bool ok = ioeUpdateRaw(0x03, 0x01, true);
    ok = ioeUpdateRaw(0x07, 0x01, false) && ok;
    ok = ioeUpdateRaw(0x05, 0x01, enable) && ok;
    uint8_t out = 0;
    if (ok && ioeReadRaw(0x05, out)) sw.high = (out & 0x01) != 0;
    if (enable && !sw.high) Serial.println("LoRa Cap: antenna switch P0 stayed low");
#else
    (void)enable;
#endif
    return sw;
}

SPIClass *selectLoraSPIBus() {
    SPIClass *bus =
        acquireSPIBus(kvxConfigPins.LoRa_bus.sck, kvxConfigPins.LoRa_bus.miso, kvxConfigPins.LoRa_bus.mosi);
    if (!bus) {
        Serial.println("No hardware SPI bus available for LoRa, falling back to default SPI");
        return &SPI;
    }
    return bus;
}

bool startLoraRadio(float bandMHz) {
    intlora = false;
    loraPacketReceived = false;
    loraInterruptEnabled = true;
    const int irqPin = getLoraIrqPin();
    if (getLoraCsPin() == GPIO_NUM_NC || kvxConfigPins.LoRa_bus.mosi == GPIO_NUM_NC ||
        kvxConfigPins.LoRa_bus.miso == GPIO_NUM_NC || kvxConfigPins.LoRa_bus.sck == GPIO_NUM_NC) {
        Serial.println("LoRa pins not configured!");
        displayError("LoRa pins not configured!", true);
        return false;
    }
    if (irqPin == GPIO_NUM_NC) {
        Serial.println("LoRa IRQ pin not configured!");
        displayError("LoRa IRQ pin not configured!", true);
        return false;
    }

    loraSpi = selectLoraSPIBus();
    clearLoraRadio();
    const int busyPin = (loraRadioVariant == LoRaRadioVariant::SX1262) ? getLoraBusyPin() : GPIO_NUM_NC;
    if (loraRadioVariant == LoRaRadioVariant::SX1262 && busyPin == GPIO_NUM_NC) {
        Serial.println("Warning: SX1262 selected but BUSY pin is not configured");
    }
    loraModule = new Module(getLoraCsPin(), irqPin, getLoraResetPin(), busyPin, *loraSpi);

#if defined(HAS_LORA_CAP)
    CapAntSwitch antSw = loraCapSetAntenna(true);
    if (antSw.high) Serial.println("LoRa Cap: antenna switch ON");
    else if (!antSw.bus) Serial.println("LoRa Cap: I2C bus down (need Cardputer-Adv)");
    else if (!antSw.present) Serial.println("LoRa Cap: no PI4IOE, antenna hardwired");
    else Serial.println("LoRa Cap: antenna switch stayed off");
#endif

    int state = RADIOLIB_ERR_NONE;
    if (loraRadioVariant == LoRaRadioVariant::SX1276) {
        lora1276 = new SX1276(loraModule);
        state = lora1276->begin(bandMHz);
        if (state == RADIOLIB_ERR_NONE) { lora1276->setDio0Action(onLoraPacket, CHANGE); }
        if (state == RADIOLIB_ERR_NONE) state = lora1276->setSpreadingFactor(spreadingFactor);
        if (state == RADIOLIB_ERR_NONE) state = lora1276->setBandwidth(SignalBandwidth / 1000.0);
        if (state == RADIOLIB_ERR_NONE) state = lora1276->setCodingRate(codingRateDenominator);
        if (state == RADIOLIB_ERR_NONE) state = lora1276->setPreambleLength(preambleLength);
        if (state == RADIOLIB_ERR_NONE) state = lora1276->startReceive();
    } else {
        lora1262 = new SX1262(loraModule);
#if defined(HAS_LORA_CAP)
        state = beginCapSx1262(bandMHz);
#else
        state = lora1262->begin(bandMHz);
#endif
        if (state == RADIOLIB_ERR_NONE) { lora1262->setDio1Action(onLoraPacket); }
        if (state == RADIOLIB_ERR_NONE) state = lora1262->setSpreadingFactor(spreadingFactor);
        if (state == RADIOLIB_ERR_NONE) state = lora1262->setBandwidth(SignalBandwidth / 1000.0);
        if (state == RADIOLIB_ERR_NONE) state = lora1262->setCodingRate(codingRateDenominator);
        if (state == RADIOLIB_ERR_NONE) state = lora1262->setPreambleLength(preambleLength);
        if (state == RADIOLIB_ERR_NONE) state = lora1262->startReceive();
    }

    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("Starting LoRa failed! Err %d\n", state);
        displayError("LoRa Init Failed", true);
        clearLoraRadio();
        return false;
    }
    intlora = true;
    Serial.println("LoRa Started");
    return true;
}

bool sendLoraMessage(String &payload) {
    if (!intlora) return false;
    loraInterruptEnabled = false;
    int state = RADIOLIB_ERR_NONE;
    if (loraRadioVariant == LoRaRadioVariant::SX1276 && lora1276) {
        state = lora1276->transmit(payload);
        lora1276->startReceive();
    } else if (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262) {
        state = lora1262->transmit(payload);
        lora1262->startReceive();
    } else {
        loraInterruptEnabled = true;
        return false;
    }
    loraInterruptEnabled = true;
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("LoRa transmit failed: %d\n", state);
        displayError("LoRa send failed");
        return false;
    }
    return true;
}

void reciveMessage() {
    if (!loraPacketReceived || !intlora) return;
    loraInterruptEnabled = false;
    loraPacketReceived = false;
    String incoming;
    int state = (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262)
                    ? lora1262->readData(incoming)
                    : (lora1276 ? lora1276->readData(incoming) : -1);
    if (state == RADIOLIB_ERR_NONE) {
        rcvmsg = incoming;
        Serial.println("Recived:" + rcvmsg);
        File file = LittleFS.open("/chats.txt", "a");
        file.println(rcvmsg);
        file.close();
        messages.push_back(rcvmsg);
        if (messages.size() > maxMessages) { scrollOffset = messages.size() - maxMessages; }
        update = true;
    } else {
        Serial.printf("LoRa read failed: %d\n", state);
    }
    if (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262) {
        lora1262->startReceive();
    } else if (lora1276) {
        lora1276->startReceive();
    }
    loraInterruptEnabled = true;
}

// render stuff

void render() {
    if (!update) return;
    tft.setTextSize(uiDenseFont()) /* LoRa chat transcript stays dense */;
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(0x6DFC);
    if (!intlora) { tft.drawString("Lora Init Failed", 10, 13); }
    Serial.println(String(displayName));
    tft.drawString("USRN: " + String(displayName), 10, 25);

    int yPos = yStart;
    int endLine = scrollOffset + maxMessages;
    if (endLine > messages.size()) endLine = messages.size();
    for (int i = scrollOffset; i < endLine; i++) {
        tft.setTextColor(kvxConfig.priColor);
        tft.drawString(messages[i], 10, yPos);
        yPos += ySpacing;
    }
    update = false;
}

void loadMessages() {
    messages.clear();
    File file = LittleFS.open("/chats.txt", "r");
    while (file.available()) {
        String line = file.readStringUntil('\n');
        messages.push_back(line);
    }
    file.close();
    if (messages.size() > maxMessages) {
        scrollOffset = messages.size() - maxMessages;
    } else {
        scrollOffset = 0;
    }
}

// optional call funcs
void sendmsg() {
    Serial.println("C bttn");
    tft.fillScreen(TFT_BLACK);
    if (!intlora) {
        tft.setTextColor(kvxConfig.priColor);

        tft.setTextColor(TFT_RED);
        tft.setTextSize(2);
        tft.setCursor(10, tftHeight / 2 - 10);
        tft.print("LoRa not init!");

        tft.drawCentreString("LoRa not initialized!", tftWidth / 2, tftHeight / 2, 2);
        delay(1500);
        update = true;
        return;
    }
    msg = keyboard(msg, 256, "Message:");
    if (msg == "\x1B") return;
    msg = String(displayName) + ": " + msg;
    if (msg == "") return;
    Serial.println(msg);
    if (!sendLoraMessage(msg)) {
        update = true;
        return;
    }
    tft.fillScreen(TFT_BLACK);
    update = true;
    File file = LittleFS.open("/chats.txt", "a");
    file.println(msg);
    file.close();

    messages.push_back(msg);
    if (messages.size() > maxMessages) { scrollOffset = messages.size() - maxMessages; }
    msg = "";
}

void upress() {
    Serial.println("Up Pressed");
    if (scrollOffset > 0) {
        scrollOffset--;
        update = true;
    }
}

void downpress() {
    Serial.println("Down Pressed");
    if (scrollOffset < messages.size() - maxMessages) {
        scrollOffset++;
        update = true;
    }
}

static void fillLoraCapDefaults(JsonDocument &doc) {
    doc["LoRa_Frequency"] = "868000000.00";
    doc["LoRa_Name"] = "kvxputer";
    doc["LoRa_Radio"] = "SX1262";
}

static void fillGenericLoraDefaults(JsonDocument &doc) {
    doc["LoRa_Frequency"] = "434500000.00";
    doc["LoRa_Name"] = "BruceTest";
    doc["LoRa_Radio"] = "SX1276";
}

static bool saveLoraSettings(JsonDocument &doc) {
    kvx::paths::ensureParentDirs(LittleFS, kvx::paths::LORA_SETTINGS);
    File file = LittleFS.open(kvx::paths::LORA_SETTINGS, "w");
    if (!file) return false;
    serializeJson(doc, file);
    file.close();
    return true;
}

static bool loadLoraSettings(JsonDocument &doc) {
    if (!LittleFS.exists(kvx::paths::LORA_SETTINGS)) return false;
    File file = LittleFS.open(kvx::paths::LORA_SETTINGS, "r");
    if (!file) return false;
    deserializeJson(doc, file);
    file.close();
    return true;
}

void applyLoraCapPinDefaults() {
#if defined(HAS_LORA_CAP)
    kvxConfigPins.gps_bus.rx = (gpio_num_t)15;
    kvxConfigPins.gps_bus.tx = (gpio_num_t)13;
    kvxConfigPins.gpsBaudrate = 115200;
#if LORA_SCK >= 0
    kvxConfigPins.LoRa_bus.sck = (gpio_num_t)LORA_SCK;
    kvxConfigPins.LoRa_bus.miso = (gpio_num_t)LORA_MISO;
    kvxConfigPins.LoRa_bus.mosi = (gpio_num_t)LORA_MOSI;
    kvxConfigPins.LoRa_bus.cs = (gpio_num_t)LORA_CS;
    kvxConfigPins.LoRa_bus.io0 = (gpio_num_t)LORA_RST;
    kvxConfigPins.LoRa_bus.io2 = (gpio_num_t)LORA_DIO0;
    pinMode(kvxConfigPins.LoRa_bus.cs, OUTPUT);
    digitalWrite(kvxConfigPins.LoRa_bus.cs, HIGH);
#endif
#endif
}

void ensureLoraSettings() {
    if (LittleFS.exists(kvx::paths::LORA_SETTINGS)) return;
    JsonDocument doc;
#if defined(HAS_LORA_CAP)
    fillLoraCapDefaults(doc);
    Serial.println("LoRa Cap: created default settings (SX1262 @ 868 MHz)");
#else
    fillGenericLoraDefaults(doc);
    Serial.println("creating lora settings .json file");
#endif
    saveLoraSettings(doc);
}

static void loadLoraRadioVariant(const JsonDocument &doc) {
#if defined(HAS_LORA_CAP)
    String stored = doc["LoRa_Radio"] | "SX1262";
#else
    String stored = doc["LoRa_Radio"] | "SX1276";
#endif
    loraRadioVariant = stored.equalsIgnoreCase("SX1262") ? LoRaRadioVariant::SX1262 : LoRaRadioVariant::SX1276;
}

void configureLoraCap() {
#if defined(HAS_LORA_CAP)
    applyLoraCapPinDefaults();
    JsonDocument doc;
    fillLoraCapDefaults(doc);
    saveLoraSettings(doc);
    loadLoraRadioVariant(doc);
    kvxConfigPins.saveFile();
    CapAntSwitch antSw = loraCapSetAntenna(true);
    String msg = "LoRa Cap configured\nGPS 115200 RX15/TX13\nLoRa SX1262 @ 868 MHz\n";
    if (antSw.high) msg += "Antenna switch ON";
    else if (!antSw.bus) msg += "Need Cardputer-Adv";
    else if (!antSw.present) msg += "LoRa868, no switch";
    else msg += "Switch enable failed";
    displayInfo(msg, true);
#else
    displayError("LoRa Cap not supported", true);
#endif
}

static void selectRadioVariantMenu() {
    JsonDocument doc;
    loadLoraSettings(doc);
    loadLoraRadioVariant(doc);
    std::vector<Option> radioOptions = {
        {"SX1276", []() {}},
        {"SX1262", []() {}}
    };
    int selected = loopOptions(
        radioOptions, MENU_TYPE_SUBMENU, "LoRa Radio", (loraRadioVariant == LoRaRadioVariant::SX1262) ? 1 : 0
    );
    if (selected >= 0) {
        loraRadioVariant = (selected == 1) ? LoRaRadioVariant::SX1262 : LoRaRadioVariant::SX1276;
        doc["LoRa_Radio"] = (loraRadioVariant == LoRaRadioVariant::SX1262) ? "SX1262" : "SX1276";
        saveLoraSettings(doc);
    }
}

static String loraCapStatusLine() {
    JsonDocument doc;
    if (!loadLoraSettings(doc)) return "Not configured";
    String radio = doc["LoRa_Radio"] | "?";
    double hz = doc["LoRa_Frequency"].as<String>().toDouble();
    double mhz = (hz > 1000) ? hz / 1000000.0 : hz;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f MHz", mhz);
    return radio + " @ " + buf;
}

void changeusername();
void chfreq();

void loraconf() {
    while (true) {
        std::vector<Option> localOptions = {
            {loraCapStatusLine(), []() {}},
            {"Radio Type",        []() { selectRadioVariantMenu(); }},
            {"Frequency",         []() { chfreq(); }                },
            {"Username",          []() { changeusername(); }         },
#if defined(HAS_LORA_CAP)
            {"Reset Cap Defaults", []() { configureLoraCap(); }},
#endif
            {"Back",              []() {}                          },
        };
        int selected = loopOptions(localOptions, MENU_TYPE_SUBMENU, "LoRa Cap");
        if (selected == -1 || selected == (int)localOptions.size() - 1) return;
    }
}

void mainloop() {
    long pressStartTime = 0;
    bool isPressing = false;
    bool breakloop = false;
    while (true) {
        render();
        reciveMessage();
        if (breakloop) { break; }
#ifdef HAS_3_BUTTONS
        if (EscPress) {
            long _tmp = millis();

            LongPress = true;
            while (EscPress) {
                if (millis() - _tmp > 200) {
                    // start drawing arc after short delay; animate over 500ms
                    int sweep = 0;
                    long elapsed = millis() - (_tmp + 200);
                    if (elapsed > 0) sweep = 360 * elapsed / 500;
                    if (sweep > 360) sweep = 360;
                    tft.drawArc(
                        tftWidth / 2,
                        tftHeight / 2,
                        25,
                        15,
                        0,
                        sweep,
                        getColorVariation(kvxConfig.priColor),
                        kvxConfig.bgColor
                    );
                }
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
            // clear arc
            tft.drawArc(
                tftWidth / 2, tftHeight / 2, 25, 15, 0, 360, kvxConfig.bgColor, kvxConfig.bgColor
            );
            LongPress = false;
            // #endif

            // decide short vs long after release
            if (millis() - _tmp > 700) {
                // long press -> exit
                breakloop = true;
            } else {
                // short press -> scroll down; consume flag first
                check(EscPress);
                upress();
            }
        }

        if (check(NextPress)) downpress();
        if (check(SelPress)) sendmsg();
#else

        if (check(NextPress)) downpress();
        if (check(EscPress)) break;
        if (check(PrevPress)) upress();
        if (check(SelPress)) sendmsg();
#endif

        delay(20);
    }
}

void lorachat() {
    if (!LittleFS.exists("/chats.txt")) {
        File file = LittleFS.open("/chats.txt", "w");
        file.close();
        Serial.println("chat file created :)");
    }
    ensureLoraSettings();
    JsonDocument doc;
    if (!loadLoraSettings(doc)) {
        displayError("LoRa settings missing", true);
        return;
    }
    displayName = doc["LoRa_Name"].as<String>();
    double BAND = doc["LoRa_Frequency"].as<String>().toDouble();
    loadLoraRadioVariant(doc);
    float bandMHz = (BAND > 1000) ? BAND / 1000000.0f : BAND;
    if (bandMHz <= 0) {
        displayError("Invalid LoRa frequency", true);
        return;
    }
    tft.fillScreen(TFT_BLACK);
    update = true;
    Serial.println("Initializing LoRa...");
    Serial.println(
        "Pins: SCK:" + String(kvxConfigPins.LoRa_bus.sck) +
        " MISO:" + String(kvxConfigPins.LoRa_bus.miso) + " MOSI:" + String(kvxConfigPins.LoRa_bus.mosi) +
        " CS:" + String(kvxConfigPins.LoRa_bus.cs) + " RST:" + String(getLoraResetPin()) +
        " IRQ:" + String(getLoraIrqPin()) + " BUSY:" + String(getLoraBusyPin()) + " BAND: " + String(bandMHz) +
        "MHz Radio: " + ((loraRadioVariant == LoRaRadioVariant::SX1262) ? "SX1262" : "SX1276") +
        " DisplayName:  " + displayName
    );

    if (!startLoraRadio(bandMHz)) {
        update = true;
        return;
    }
    tft.setTextWrap(true, true);
    tft.setTextDatum(TL_DATUM);
    loadMessages();
    mainloop();
}

// settings
// check the saving and loading
void changeusername() {
    tft.fillScreen(TFT_BLACK);
    String username = keyboard(username, 64, "");
    if (username == "" || username == "\x1B") return;
    File file = LittleFS.open(kvx::paths::LORA_SETTINGS, "r");
    JsonDocument doc;
    deserializeJson(doc, file);
    file.close();
    doc["LoRa_Name"] = username;
    file = LittleFS.open(kvx::paths::LORA_SETTINGS, "w");
    serializeJson(doc, file);
    file.close();
}

void chfreq() {
    tft.fillScreen(TFT_BLACK);
    char buf[15];
    File file = LittleFS.open(kvx::paths::LORA_SETTINGS, "r");
    JsonDocument doc;
    deserializeJson(doc, file);
    file.close();

    double dfreq = doc["LoRa_Frequency"].as<String>().toDouble();
    dfreq = dfreq / 1000000;
    snprintf(buf, sizeof(buf), "%.3f", dfreq);
    String freq = num_keyboard(buf, 12, "in Mhz");
    dfreq = freq.toDouble();
    if (dfreq == 0 || freq == "\x1B") {
        displayError("Invalid value");
        return;
    } else if (dfreq > 1000) {
        displayError("Invalid value, Exceeds 1Ghz");
        return;
    }
    dfreq = dfreq * 1000000;
    snprintf(buf, sizeof(buf), "%.2f", dfreq);
    doc["LoRa_Frequency"] = buf;

    file = LittleFS.open(kvx::paths::LORA_SETTINGS, "w");
    serializeJson(doc, file);
    file.close();
}

struct LoraBandPreset {
    const char *label;
    float mhz;
};

struct RssiWin {
    float mean = 0;
    float peak = 0;
    float span = 0;
    int n = 0;
    bool abort = false;
};

static const LoraBandPreset *antBands(int &count) {
#if defined(HAS_LORA_CAP)
    static const LoraBandPreset bands[] = {
        {"868.0 EU", 868.0f}, {"869.525 EU", 869.525f}, {"915.0 US", 915.0f},
        {"920.0", 920.0f},    {"923.0 AS", 923.0f},
    };
#else
    static const LoraBandPreset bands[] = {
        {"433.92", 433.92f},
        {"868.0", 868.0f},
        {"915.0", 915.0f},
    };
#endif
    count = (int)(sizeof(bands) / sizeof(bands[0]));
    return bands;
}

static int mhzToKhz(float mhz) { return (int)(mhz * 1000.0f + 0.5f); }

static bool mhzAllowed(float mhz) {
#if defined(HAS_LORA_CAP)
    return mhz >= 868.0f && mhz <= 923.0f;
#else
    return mhz >= 150.0f && mhz <= 960.0f;
#endif
}

static bool antLeave() { return forceHome || returnToMenu; }

static float savedLoraMhz() {
    JsonDocument doc;
    if (!loadLoraSettings(doc)) {
#if defined(HAS_LORA_CAP)
        return 868.0f;
#else
        return 434.5f;
#endif
    }
    double hz = doc["LoRa_Frequency"].as<String>().toDouble();
    if (hz > 1000.0) hz /= 1000000.0;
    if (hz <= 0.0) {
#if defined(HAS_LORA_CAP)
        return 868.0f;
#else
        return 434.5f;
#endif
    }
    return (float)hz;
}

static bool saveLoraMhz(float mhz) {
    JsonDocument doc;
    if (!loadLoraSettings(doc)) {
#if defined(HAS_LORA_CAP)
        fillLoraCapDefaults(doc);
#else
        fillGenericLoraDefaults(doc);
#endif
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", (double)mhz * 1000000.0);
    doc["LoRa_Frequency"] = buf;
    return saveLoraSettings(doc);
}

static const char *capLine(const CapAntSwitch &sw) {
#if !defined(HAS_LORA_CAP)
    (void)sw;
    return "No cap antenna switch";
#else
    if (!sw.bus) return "No Adv I2C bus";
    if (!sw.present) return "LoRa868 antenna hardwired";
    if (sw.high) return "LoRa-1262 switch ON";
    return "LoRa-1262 switch OFF";
#endif
}

static int antRadioErr = 0;
static bool antTcxo = false;

static bool antStartRadio(float mhz) {
    intlora = false;
    loraPacketReceived = false;
    loraInterruptEnabled = false;
    antRadioErr = 0;
    antTcxo = false;
    const int irqPin = getLoraIrqPin();
    if (getLoraCsPin() == GPIO_NUM_NC || kvxConfigPins.LoRa_bus.mosi == GPIO_NUM_NC ||
        kvxConfigPins.LoRa_bus.miso == GPIO_NUM_NC || kvxConfigPins.LoRa_bus.sck == GPIO_NUM_NC) {
        antRadioErr = -1;
        return false;
    }
    if (irqPin == GPIO_NUM_NC) {
        antRadioErr = -2;
        return false;
    }
#if defined(HAS_LORA_CAP)
    loraCapSetAntenna(true);
#endif
    loraSpi = selectLoraSPIBus();
    clearLoraRadio();
#if defined(HAS_LORA_CAP)
    const bool sx1262 = true;
#else
    const bool sx1262 = loraRadioVariant == LoRaRadioVariant::SX1262;
#endif
    const int busyPin = sx1262 ? getLoraBusyPin() : GPIO_NUM_NC;
    loraModule = new Module(getLoraCsPin(), irqPin, getLoraResetPin(), busyPin, *loraSpi);
    int16_t state;
    if (sx1262) {
        lora1262 = new SX1262(loraModule);
#if defined(HAS_LORA_CAP)
        state = beginCapSx1262(mhz);
        antTcxo = loraCapTcxoOn;
#else
        state = lora1262->begin(mhz);
#endif
        if (state == RADIOLIB_ERR_NONE) state = lora1262->startReceive();
    } else {
        lora1276 = new SX1276(loraModule);
        state = lora1276->begin(mhz);
        if (state == RADIOLIB_ERR_NONE) state = lora1276->startReceive();
    }
    antRadioErr = state;
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("Antenna check init failed: %d\n", state);
        clearLoraRadio();
        return false;
    }
    return true;
}

static void antStopRadio() {
    loraInterruptEnabled = false;
    if (lora1262) lora1262->sleep();
    else if (lora1276) lora1276->sleep();
    clearLoraRadio();
    intlora = false;
#if defined(HAS_LORA_CAP)
    loraCapSetAntenna(true);
#endif
}

static void showAntRadioFail(const CapAntSwitch &sw) {
    const char *why = "init failed";
    if (antRadioErr == -1) why = "pins not set";
    else if (antRadioErr == -2) why = "IRQ missing";
    char buf[96];
    snprintf(buf, sizeof(buf), "Radio %s (%d). %s", why, antRadioErr, capLine(sw));
    displayError(buf, true);
}

static bool antTune(float mhz) {
    int16_t state = RADIOLIB_ERR_NONE;
    if (lora1262) {
        state = lora1262->setFrequency(mhz);
        if (state == RADIOLIB_ERR_NONE) state = lora1262->startReceive();
    } else if (lora1276) {
        state = lora1276->setFrequency(mhz);
        if (state == RADIOLIB_ERR_NONE) state = lora1276->startReceive();
    } else {
        return false;
    }
    return state == RADIOLIB_ERR_NONE;
}

static float antReadRssi() {
    if (lora1262) return lora1262->getRSSI(false);
    if (lora1276) return lora1276->getRSSI();
    return 0;
}

static RssiWin antSample(int count, int gapMs) {
    RssiWin w;
    float sum = 0;
    float peak = -300;
    float lo = 50;
    for (int i = 0; i < count; i++) {
        if (check(EscPress) || antLeave()) {
            w.abort = true;
            break;
        }
        float r = antReadRssi();
        if (r < -1.0f && r > -140.0f) {
            sum += r;
            if (r > peak) peak = r;
            if (r < lo) lo = r;
            w.n++;
        }
        delay(gapMs);
    }
    if (w.n > 0) {
        w.mean = sum / w.n;
        w.peak = peak;
        w.span = peak - lo;
    }
    return w;
}

static bool antCompare(RssiWin &on, RssiWin &off) {
#if !defined(HAS_LORA_CAP)
    (void)on;
    (void)off;
    return false;
#else
    CapAntSwitch sw = loraCapSetAntenna(true);
    if (!sw.present) return false;
    delay(20);
    on = antSample(8, 12);
    if (on.abort) {
        loraCapSetAntenna(true);
        return false;
    }
    loraCapSetAntenna(false);
    delay(20);
    off = antSample(8, 12);
    loraCapSetAntenna(true);
    delay(10);
    if (lora1262) lora1262->startReceive();
    else if (lora1276) lora1276->startReceive();
    return !off.abort;
#endif
}

static void antJudge(
    bool haveSwitch, const RssiWin &on, const RssiWin &off, const char *&text, uint16_t &color
) {
    if (!haveSwitch) {
        if (on.n < 3) {
            text = "No RSSI from radio";
            color = TFT_RED;
        } else if (on.span >= 2.0f) {
            text = "RF path is live";
            color = TFT_GREEN;
        } else {
            text = "RSSI is flat";
            color = TFT_YELLOW;
        }
        return;
    }
    if (on.n < 3 || off.n < 3) {
        text = "RSSI read failed";
        color = TFT_RED;
        return;
    }
    if ((on.mean - off.mean) >= 3.0f) {
        text = "Antenna connected";
        color = TFT_GREEN;
    } else if (on.mean > -105.0f) {
        text = "RF energy present";
        color = TFT_GREEN;
    } else {
        text = "Antenna looks open";
        color = TFT_YELLOW;
    }
}

static char antFreqLabel[24];

static bool pickLoraCheckFreq(float &mhz, const char *&label, bool &detect) {
    int n = 0;
    const LoraBandPreset *bands = antBands(n);
    while (!antLeave()) {
        std::vector<Option> opts;
        opts.push_back({"Detect band", []() {}});
        for (int i = 0; i < n; i++) opts.push_back({bands[i].label, []() {}});
        float saved = savedLoraMhz();
        char savedLbl[28];
        snprintf(savedLbl, sizeof(savedLbl), "Saved %.3f", saved);
        opts.push_back({savedLbl, []() {}});
        opts.push_back({"Custom MHz", []() {}});
        opts.push_back({"Back", []() {}});
        const int back = (int)opts.size() - 1;
        const int custom = back - 1;
        const int savedIdx = custom - 1;
#if defined(HAS_LORA_CAP)
        const char *title = "Antenna 868-923";
#else
        const char *title = "Antenna Check";
#endif
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, title, 0);
        if (antLeave() || sel < 0 || sel == back) return false;
        detect = false;
        if (sel == 0) {
            detect = true;
            mhz = bands[0].mhz;
            label = "detect";
            return true;
        }
        if (sel >= 1 && sel <= n) {
            mhz = bands[sel - 1].mhz;
            label = bands[sel - 1].label;
            return true;
        }
        if (sel == savedIdx) {
            if (!mhzAllowed(saved)) {
#if defined(HAS_LORA_CAP)
                displayError("Saved freq is outside 868-923", true);
#else
                displayError("Saved freq is outside 150-960", true);
#endif
                continue;
            }
            mhz = saved;
            snprintf(antFreqLabel, sizeof(antFreqLabel), "saved");
            label = antFreqLabel;
            return true;
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "%.3f", saved);
#if defined(HAS_LORA_CAP)
        String entered = num_keyboard(buf, 12, "868-923 MHz");
#else
        String entered = num_keyboard(buf, 12, "MHz");
#endif
        if (entered == "\x1B") continue;
        float customMhz = entered.toFloat();
        if (!mhzAllowed(customMhz)) {
#if defined(HAS_LORA_CAP)
            displayError("Cap band is 868-923 MHz", true);
#else
            displayError("Use 150-960 MHz", true);
#endif
            continue;
        }
        mhz = customMhz;
        snprintf(antFreqLabel, sizeof(antFreqLabel), "custom");
        label = antFreqLabel;
        detect = false;
        return true;
    }
    return false;
}

static bool antScanBands(float *means, int &best, bool &activity) {
    int n = 0;
    const LoraBandPreset *bands = antBands(n);
    if (n > 8) n = 8;
    best = 0;
    activity = false;
    float bestMean = -300;
    float worst = 20;
    const int sz = uiDenseFont();
    const int lh = uiLineH(sz);
    for (int i = 0; i < n; i++) {
        if (antLeave()) return false;
        resetPowerSaveTimer();
        tft.fillScreen(kvxConfig.bgColor);
        tft.setTextSize(sz);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        char line[40];
        snprintf(line, sizeof(line), "Scanning %s", bands[i].label);
        tft.drawString(line, 4, 2);
        int y = 2 + lh;
        for (int j = 0; j < i; j++) {
            if (means[j] < -190) snprintf(line, sizeof(line), "%s  --", bands[j].label);
            else snprintf(line, sizeof(line), "%s  %d", bands[j].label, (int)means[j]);
            tft.drawString(line, 4, y);
            y += lh;
        }
        tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
        tft.drawString("Esc cancels", 4, tftHeight - lh - 2);
        if (!antTune(bands[i].mhz)) {
            means[i] = -200;
            continue;
        }
        delay(25);
        RssiWin w = antSample(6, 15);
        if (w.abort) return false;
        means[i] = (w.n > 0) ? w.mean : -200;
        if (means[i] > -190) {
            if (means[i] > bestMean) {
                bestMean = means[i];
                best = i;
            }
            if (means[i] < worst) worst = means[i];
        }
    }
    activity = bestMean > -190.0f && worst < 0.0f && (bestMean - worst) >= 6.0f;
    return true;
}

static bool antScanChoice(const float *means, int best, bool activity, float &mhz, const char *&label) {
    int n = 0;
    const LoraBandPreset *bands = antBands(n);
    if (n > 8) n = 8;
    bool any = false;
    for (int i = 0; i < n; i++) {
        if (means[i] > -190) any = true;
    }
    if (!any) {
        displayError("No RSSI on any band", true);
        return false;
    }
    std::vector<Option> opts;
    char lines[8][32];
    for (int i = 0; i < n; i++) {
        if (means[i] < -190) snprintf(lines[i], sizeof(lines[i]), "%s  --", bands[i].label);
        else {
            snprintf(
                lines[i], sizeof(lines[i]), "%s  %d%s", bands[i].label, (int)means[i],
                (i == best && activity) ? " *" : ""
            );
        }
        opts.push_back({lines[i], []() {}});
    }
    opts.push_back({activity ? "Watch peak" : "Watch strongest", []() {}});
    opts.push_back({"Save strongest", []() {}});
    opts.push_back({"Back", []() {}});
    int sel = loopOptions(opts, MENU_TYPE_SUBMENU, activity ? "Signal found" : "Band is quiet", 0);
    const int watch = n;
    const int save = n + 1;
    const int back = n + 2;
    if (antLeave() || sel < 0 || sel == back) return false;
    int chosen = best;
    if (sel >= 0 && sel < n) chosen = sel;
    mhz = bands[chosen].mhz;
    label = bands[chosen].label;
    if (sel == save) {
        mhz = bands[best].mhz;
        label = bands[best].label;
        if (saveLoraMhz(mhz)) displaySuccess("Frequency saved", true);
        else displayError("Save failed", true);
    } else if (sel == watch) {
        mhz = bands[best].mhz;
        label = bands[best].label;
    }
    (void)watch;
    return antTune(mhz);
}

static void drawAntHud(
    const char *cap, const char *radio, const char *freq, const char *rssi, const char *delta,
    const char *verdict, uint16_t verdictColor, const char *hint
) {
    const int sz = uiDenseFont();
    const int lh = uiLineH(sz);
    tft.fillScreen(kvxConfig.bgColor);
    tft.setTextSize(sz);
    tft.setTextDatum(TL_DATUM);
    int y = 2;
    auto line = [&](const char *text, uint16_t color) {
        if (y > tftHeight - lh) return;
        tft.setTextColor(color, kvxConfig.bgColor);
        tft.drawString(text, 4, y);
        y += lh;
    };
    line("Antenna Check", kvxConfig.priColor);
    line(cap, kvxConfig.priColor);
    line(radio, kvxConfig.priColor);
    line(freq, kvxConfig.priColor);
    line(rssi, kvxConfig.priColor);
    line(delta, kvxConfig.priColor);
    line(verdict, verdictColor);
    line(hint, kvxConfig.secColor);
    line("<> freq   Sel test   s save", kvxConfig.secColor);
}

static void antLive(float mhz, const char *label) {
    int n = 0;
    const LoraBandPreset *bands = antBands(n);
    int idx = -1;
    int want = mhzToKhz(mhz);
    for (int i = 0; i < n; i++) {
        if (mhzToKhz(bands[i].mhz) == want) idx = i;
    }
    CapAntSwitch sw = loraCapSetAntenna(true);
    RssiWin on;
    RssiWin off;
    bool compared = false;
    const char *verdict = "Listening";
    uint16_t verdictColor = kvxConfig.priColor;
    if (sw.present && sw.high) {
        tft.fillScreen(kvxConfig.bgColor);
        tft.setTextSize(uiDenseFont());
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        tft.drawString("Testing antenna path", 4, 8);
        compared = antCompare(on, off);
        if (on.abort || off.abort) return;
        if (compared) antJudge(true, on, off, verdict, verdictColor);
    } else if (!sw.present) {
        on = antSample(8, 12);
        if (on.abort) return;
        antJudge(false, on, off, verdict, verdictColor);
    } else {
        verdict = "Switch is off";
        verdictColor = TFT_RED;
    }
    sw = loraCapSetAntenna(true);
    bool saveHeld = false;
    unsigned long savedAt = 0;
    unsigned long lastLog = 0;
    float recent[8] = {};
    int recentN = 0;
    int recentI = 0;

    while (!antLeave()) {
        resetPowerSaveTimer();
        if (check(EscPress)) return;
        bool retune = false;
        if (check(NextPress)) {
            idx = (idx < 0) ? 0 : (idx + 1) % n;
            retune = true;
        } else if (check(PrevPress)) {
            idx = (idx < 0) ? n - 1 : (idx - 1 + n) % n;
            retune = true;
        }
        if (retune) {
            mhz = bands[idx].mhz;
            label = bands[idx].label;
            compared = false;
            recentN = 0;
            if (!antTune(mhz)) {
                verdict = "Tune failed";
                verdictColor = TFT_RED;
            } else {
                verdict = "Retuned. Sel retests";
                verdictColor = kvxConfig.priColor;
            }
        }
        if (check(SelPress) && sw.present) {
            tft.fillScreen(kvxConfig.bgColor);
            tft.setTextSize(uiDenseFont());
            tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            tft.drawString("Testing antenna path", 4, 8);
            compared = antCompare(on, off);
            if (on.abort || off.abort) return;
            sw = loraCapSetAntenna(true);
            if (!sw.high) {
                verdict = "Switch is off";
                verdictColor = TFT_RED;
            } else if (compared) {
                antJudge(true, on, off, verdict, verdictColor);
            }
        } else if (check(SelPress)) {
            on = antSample(8, 12);
            if (on.abort) return;
            antJudge(false, on, off, verdict, verdictColor);
        }
        char letter = checkLetterShortcutPress();
        bool saveKey = letter == 's' || letter == 'S';
        if (saveKey && !saveHeld) {
            saveHeld = true;
            if (saveLoraMhz(mhz)) savedAt = millis();
            else displayError("Save failed", true);
        }
        if (!saveKey) saveHeld = false;

        float live = antReadRssi();
        bool liveOk = live < -1.0f && live > -140.0f;
        if (liveOk) {
            recent[recentI] = live;
            recentI = (recentI + 1) % 8;
            if (recentN < 8) recentN++;
        }
        float span = 0;
        if (recentN >= 2) {
            float hi = -300;
            float lo = 50;
            for (int i = 0; i < recentN; i++) {
                if (recent[i] > hi) hi = recent[i];
                if (recent[i] < lo) lo = recent[i];
            }
            span = hi - lo;
        }
        char radio[32];
        if (lora1262) snprintf(radio, sizeof(radio), antTcxo ? "SX1262 OK  TCXO 3V" : "SX1262 OK");
        else snprintf(radio, sizeof(radio), "SX1276 OK");
        char freq[32];
        snprintf(freq, sizeof(freq), "%.3f MHz  %s", mhz, label);
        char rssi[32];
        if (liveOk) snprintf(rssi, sizeof(rssi), "RSSI %d dBm", (int)live);
        else snprintf(rssi, sizeof(rssi), "RSSI --");
        char delta[40];
        if (compared && on.n > 0 && off.n > 0) {
            snprintf(
                delta, sizeof(delta), "ON %d  OFF %d  %+d dB", (int)on.mean, (int)off.mean,
                (int)(on.mean - off.mean)
            );
        } else if (sw.present) {
            snprintf(delta, sizeof(delta), "Sel compares the switch");
        } else {
            snprintf(delta, sizeof(delta), "Antenna is hardwired");
        }
        const char *hint = "Touch the antenna";
        if (savedAt != 0 && millis() - savedAt < 1500) hint = "Saved to LoRa settings";
        else if (span >= 3.0f) hint = "RSSI is moving";
        drawAntHud(capLine(sw), radio, freq, rssi, delta, verdict, verdictColor, hint);
        if (millis() - lastLog > 1000) {
            lastLog = millis();
            Serial.printf("Antenna check %.3f MHz RSSI %.1f (%s)\n", mhz, live, verdict);
        }
        delay(80);
    }
}

void loraAntennaCheck() {
    wakeUpScreen();
    ensureLoraSettings();
    Serial.println("Antenna Check");
    while (!antLeave()) {
        float mhz = 868.0f;
        const char *label = "868.0 EU";
        bool detect = false;
        if (!pickLoraCheckFreq(mhz, label, detect)) return;
        CapAntSwitch sw = loraCapSetAntenna(true);
        if (!antStartRadio(mhz)) {
            showAntRadioFail(sw.bus ? loraCapSetAntenna(true) : sw);
            continue;
        }
        if (detect) {
            float means[8];
            for (int i = 0; i < 8; i++) means[i] = -200;
            int best = 0;
            bool activity = false;
            if (!antScanBands(means, best, activity)) {
                antStopRadio();
                continue;
            }
            if (!antScanChoice(means, best, activity, mhz, label)) {
                antStopRadio();
                continue;
            }
        }
        antLive(mhz, label);
        antStopRadio();
    }
}
#endif
