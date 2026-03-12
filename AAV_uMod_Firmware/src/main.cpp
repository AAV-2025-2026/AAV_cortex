#include <Arduino.h>
#include "globals.h"
#include "hardware_configs.h"
#include "websoc_serial/websoc_serial.h"
#include "dac/dac.h"
#include "jrk_g2/jrk_g2.h"
#include "rc_handler/rc_handler.h"
#include "ros2_handler/ros2_handler.h"
#include "web_handler/web_handler.h"

// ── WiFi credentials ──────────────────────────────
const char* ssid     = "AAVwifi";
const char* password = "aav@2023";

// ── Global object definitions ──────────────────────────────

WebServer        server(80);
HTTPUpdateServer httpUpdater;
WebSocketsServer ws(81);
IBusBM           ibus;
PCA9555          ioex;
JrkG2I2C         jrk_steer(STEER_ID);
JrkG2I2C         jrk_brake(BRAKE_ID);

float current_steering = 0.0f;
float current_speed    = 0.0f;
float current_accel    = 0.0f;
bool  jrk_steer_ok     = false;
bool  jrk_brake_ok     = false;

WSSerial WSerial;

// ── setup ──────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1000);

    String earlyLog = "\n=== ESP32: AAV Controller ===\n";

    pinMode(LED_PIN,       OUTPUT);
    pinMode(RED_LED_PIN,   OUTPUT);
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(BLUE_LED_PIN,  OUTPUT);
    digitalWrite(LED_PIN,        HIGH);
    digitalWrite(RED_LED_PIN,    LOW);
    digitalWrite(GREEN_LED_PIN,  LOW);
    digitalWrite(BLUE_LED_PIN,   LOW);

    // WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("WiFi connecting");
    earlyLog += "WiFi connecting";
    unsigned long wifi_start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifi_start < 10000) {
        delay(500); Serial.print("."); earlyLog += ".";
    }
    if (WiFi.status() == WL_CONNECTED) {
        server.on("/",       HTTP_GET, handleStatus);
        server.on("/status", HTTP_GET, handleStatus);
        server.on("/scan",   HTTP_GET, handleScan);
        httpUpdater.setup(&server);
        server.begin();
        ws.begin();
        char ipbuf[80];
        snprintf(ipbuf, sizeof(ipbuf), "\nWiFi -> %s", WiFi.localIP().toString().c_str());
        earlyLog += ipbuf;
        Serial.println(ipbuf);
        Serial.printf("OTA  -> http://%s/update\n", WiFi.localIP().toString().c_str());
        Serial.printf("Scan -> http://%s/scan\n",   WiFi.localIP().toString().c_str());
        digitalWrite(GREEN_LED_PIN, HIGH);
    } else {
        earlyLog += "\nWiFi failed";
        Serial.println("\nWiFi failed");
        digitalWrite(RED_LED_PIN, HIGH);
    }

    // Flush early log to WS
    int start = 0;
    for (int i = 0; i <= (int)earlyLog.length(); i++) {
        if (i == (int)earlyLog.length() || earlyLog[i] == '\n') {
            String line = earlyLog.substring(start, i);
            line.trim();
            if (line.length() > 0) ws.broadcastTXT(line);
            start = i + 1;
        }
    }

    // Serial1 — ROS UART
    USER_SERIAL.println("Initializing Serial1 (ROS UART) @ GPIO16/17");
    Serial1.begin(115200, SERIAL_8N1, 16, 17);

    // iBUS
    pinMode(IBUS_RX_PIN, INPUT_PULLUP);
    Serial2.begin(IBUS_BAUD, SERIAL_8N1, IBUS_RX_PIN, IBUS_TX_PIN);
    delay(100);
    ibus.begin(Serial2, IBUSBM_NOTIMER);
    USER_SERIAL.println("iBUS Serial2 @ GPIO32/33");

    // I2C
    USER_SERIAL.println("Initializing I2C...");
    unsigned long i2c_start = millis();
    while (!Wire.begin(I2C_SDA, I2C_SCL)) {
        server.handleClient(); ws.loop();
        USER_SERIAL.println("I2C init failed, retrying...");
        delay(100);
        if (millis() - i2c_start > 3000) {
            USER_SERIAL.println("WARNING: I2C failed after 3s");
            break;
        }
    }
    USER_SERIAL.printf("I2C -> SDA:%d SCL:%d\n", I2C_SDA, I2C_SCL);

    // PCA9555
    ioex.attach(Wire);
    ioex.polarity(PCA95x5::Polarity::ORIGINAL_ALL);
    ioex.direction(PCA95x5::Direction::OUT_ALL);
    ioex.write(PCA95x5::Level::H_ALL);
    USER_SERIAL.println("PCA9555 ready");

    // DAC
    USER_SERIAL.println("Connecting to DAC...");
    unsigned long dac_start = millis();
    bool dac_ready = false;
    while (millis() - dac_start < 3000) {
        server.handleClient(); ws.loop();
        if (detectDAC()) { dac_ready = true; break; }
        USER_SERIAL.println("DAC not found, retrying...");
        delay(200);
    }
    if (dac_ready) {
        writeDAC(0);
        USER_SERIAL.println("DAC OK");
    } else {
        USER_SERIAL.println("WARNING: DAC not found — no throttle");
        digitalWrite(RED_LED_PIN, HIGH);
    }

    // JRK G2
    USER_SERIAL.println("Initializing JRK G2 controllers...");
    if (jrkPing(STEER_ID)) {
        jrk_steer_ok = true;
        jrk_steer.setTarget(JRK_CENTER);
        USER_SERIAL.printf("JRK STEER (0x%02X) OK — centered (%d)\n", STEER_ID, JRK_CENTER);
    } else {
        USER_SERIAL.printf("WARNING: JRK STEER (0x%02X) not found!\n", STEER_ID);
    }
    if (jrkPing(BRAKE_ID)) {
        jrk_brake_ok = true;
        jrk_brake.setTarget(JRK_MAX);
        USER_SERIAL.printf("JRK BRAKE (0x%02X) OK — engaged (%d)\n", BRAKE_ID, JRK_MAX);
    } else {
        USER_SERIAL.printf("WARNING: JRK BRAKE (0x%02X) not found!\n", BRAKE_ID);
    }

    USER_SERIAL.println("Ready — SwA: RC_EN | SwC: TOP=Brake, MID=Fwd, LOW=Rev");
}

// ── loop ───────────────────────────────────────────────────
void loop() {
    static uint8_t buf[13];
    static uint8_t idx = 0;

    server.handleClient();
    ws.loop();
    ibus.loop();

    uint16_t rc_en_raw = ibus.readChannel(RC_EN_CH);
    bool rc_mode = (rc_en_raw > 1500 && rc_en_raw < 2200);

    if (rc_mode) {
        handleRCMode();
    } else {
        handleROS2Messages(buf, idx);
    }

    static unsigned long last_status = 0;
    if (millis() - last_status > 200) {
        publishStatus();
        last_status = millis();
    }

    delay(1);
}
