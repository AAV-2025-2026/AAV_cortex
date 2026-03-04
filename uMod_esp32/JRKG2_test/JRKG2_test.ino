#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include <IBusBM.h>
#include <Wire.h>
#include <JrkG2.h>
#include <WebSocketsServer.h>
#include <PCA95x5.h>

// ============================================================
// Pin / Channel definitions
// ============================================================
#define IBUS_RX_PIN    32
#define IBUS_TX_PIN    33
#define IBUS_BAUD      115200

#define RC_EN_CH       5
#define THR_RC_CH      2
#define STR_RC_CH      3
#define DRIVE_MODE_CH  4

#define LED_PIN        2
#define RED_LED_PIN    18
#define GREEN_LED_PIN  19
#define BLUE_LED_PIN   14

#define DIR_PIN        0
#define I2C_SDA        21
#define I2C_SCL        22

#define STEER_ID       0x05
#define BRAKE_ID       0x04
#define DAC_ADDR       0x0C

// JRK target range: 0–4095, center = 2048
#define JRK_CENTER     2048
#define JRK_MIN        0
#define JRK_MAX        4095

// ============================================================
// WiFi credentials
// ============================================================
const char* ssid     = "AAVwifi";
const char* password = "aav@2023";

// ============================================================
// Globals
// ============================================================
WebServer        server(80);
HTTPUpdateServer httpUpdater;
WebSocketsServer ws(81);
IBusBM           ibus;
PCA9555          ioex;
JrkG2I2C         jrk_steer(STEER_ID);
JrkG2I2C         jrk_brake(BRAKE_ID);

float current_steering = 0.0;
float current_speed    = 0.0;
float current_accel    = 0.0;

bool jrk_steer_ok = false;
bool jrk_brake_ok = false;

// ============================================================
// WSSerial
// ============================================================
class WSSerial : public Print {
public:
    size_t write(uint8_t c) override {
        Serial.write(c);
        _buf += (char)c;
        if (c == '\n') {
            _buf.trim();
            if (_buf.length() > 0) ws.broadcastTXT(_buf);
            _buf = "";
        }
        return 1;
    }
    size_t write(const uint8_t* buffer, size_t size) override {
        for (size_t i = 0; i < size; i++) write(buffer[i]);
        return size;
    }
private:
    String _buf;
};

WSSerial WSerial;
#define USER_SERIAL WSerial

// ============================================================
// JRK helpers
// ============================================================

// Ping a JRK — returns true if it ACKs on I2C
bool jrkPing(uint8_t addr) {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

// Set steering target with error guard
void setSteer(uint16_t target) {
    if (!jrk_steer_ok) return;
    target = constrain(target, JRK_MIN, JRK_MAX);
    jrk_steer.setTarget(target);

    // Check for errors and log them
    uint16_t err = jrk_steer.getErrorFlagsHalting();
    if (err) {
        USER_SERIAL.printf("JRK STEER ERR: 0x%04X — clearing\n", err);
        uint16_t err = jrk_steer.getErrorFlagsHalting();
        if (err) USER_SERIAL.printf("JRK STEER ERR: 0x%04X\n", err);
    }
}

// Set brake target with error guard
// Brake JRK: full engage = JRK_MAX (4095), full release = JRK_MIN (0)
// Adjust mapping below to match your physical brake actuator direction
void setBrake(uint16_t target) {
    if (!jrk_brake_ok) return;
    target = constrain(target, JRK_MIN, JRK_MAX);
    jrk_brake.setTarget(target);

    uint16_t err = jrk_brake.getErrorFlagsHalting();
    if (err) {
        USER_SERIAL.printf("JRK BRAKE ERR: 0x%04X — clearing\n", err);
        uint16_t err = jrk_steer.getErrorFlagsHalting();
        if (err) USER_SERIAL.printf("JRK BRAKE ERR: 0x%04X\n", err);
    }
}

// Emergency stop — centers steer, full brake
void jrkSafeStop() {
    setSteer(JRK_CENTER);
    setBrake(JRK_MAX);   // full brake engage — verify direction for your actuator
    writeDAC(0);
    USER_SERIAL.println("JRK SAFE STOP");
}

// ============================================================
// DAC
// ============================================================
void writeDAC(uint16_t value) {
    value &= 0x3FF;
    uint8_t upperByte = (value >> 6) & 0x0F;
    uint8_t lowerByte = (value << 2) & 0xFC;
    Wire.beginTransmission(DAC_ADDR);
    Wire.write(upperByte);
    Wire.write(lowerByte);
    if (Wire.endTransmission() != 0) {
        USER_SERIAL.println("ERROR: DAC did not acknowledge!");
    }
}

bool detectDAC() {
    Wire.beginTransmission(DAC_ADDR);
    return (Wire.endTransmission() == 0);
}

// ============================================================
// PCA9555
// ============================================================
void ext_digitalWrite(uint8_t pin, bool value) {
    if (value)
        ioex.write(static_cast<PCA95x5::Port::Port>(pin), PCA95x5::Level::H);
    else
        ioex.write(static_cast<PCA95x5::Port::Port>(pin), PCA95x5::Level::L);
}

// ============================================================
// publishStatus
// ============================================================
void publishStatus() {
    char buf[80];
    snprintf(buf, sizeof(buf), "STATUS:%.2f,%.2f,%.2f | STR:%s BRK:%s",
             current_steering, current_speed, current_accel,
             jrk_steer_ok ? "OK" : "FAIL",
             jrk_brake_ok ? "OK" : "FAIL");
    USER_SERIAL.println(buf);
}

// ============================================================
// HTTP status page
// ============================================================
void handleStatus() {
    String html = R"(<!DOCTYPE html>
<html>
<head>
  <title>AAV Controller</title>
  <style>
    body { font-family: sans-serif; padding: 20px; background: #1a1a1a; color: #eee; }
    h2   { color: #4fc3f7; }
    h3   { color: #81c784; }
    .val { font-weight: bold; color: #fff; }
    .ok  { color: #00e676; }
    .err { color: #ff5252; }
    #log {
      font-family: monospace; font-size: 13px;
      background: #111; color: #00e676;
      height: 350px; overflow-y: auto;
      padding: 10px; border: 1px solid #333; border-radius: 4px;
    }
    a { color: #4fc3f7; }
  </style>
</head>
<body>
  <h2>AAV Controller</h2>
  <p>Steering: <span class='val' id='steer'>--</span></p>
  <p>Speed:    <span class='val' id='speed'>--</span></p>
  <p>Accel:    <span class='val' id='accel'>--</span></p>
  <p>JRK Steer: <span class='val' id='jrkstr'>--</span> | JRK Brake: <span class='val' id='jrkbrk'>--</span></p>
  <hr>
  <h3>Serial Log</h3>
  <div id='log'></div>
  <br><a href='/update'>OTA Update</a> | <a href='/scan'>I2C Scan</a>
  <script>
    var ws = new WebSocket('ws://' + location.hostname + ':81/');
    ws.onmessage = function(e) {
      var log = document.getElementById('log');
      var line = document.createElement('div');
      line.textContent = e.data;
      log.appendChild(line);
      log.scrollTop = log.scrollHeight;
      if (e.data.startsWith('STATUS:')) {
        var main = e.data.replace('STATUS:','').split(' | ');
        var parts = main[0].split(',');
        document.getElementById('steer').textContent = parts[0];
        document.getElementById('speed').textContent = parts[1];
        document.getElementById('accel').textContent = parts[2];
        if (main[1]) {
          var jrk = main[1].split(' ');
          var strEl = document.getElementById('jrkstr');
          var brkEl = document.getElementById('jrkbrk');
          strEl.textContent = jrk[0].replace('STR:','');
          brkEl.textContent = jrk[1].replace('BRK:','');
          strEl.className = strEl.textContent === 'OK' ? 'val ok' : 'val err';
          brkEl.className = brkEl.textContent === 'OK' ? 'val ok' : 'val err';
        }
      }
    };
    ws.onclose = function() {
      document.getElementById('log').innerHTML +=
        '<div style="color:red">-- disconnected --</div>';
    };
  </script>
</body>
</html>)";
    server.send(200, "text/html", html);
}

// ============================================================
// I2C Scan
// ============================================================
void handleScan() {
    String result = "=== I2C Scan ===\n";
    bool found = false;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            found = true;
            char line[60];
            snprintf(line, sizeof(line), "  Found: 0x%02X%s%s%s%s", addr,
                addr == STEER_ID ? " <- JRK steer"   : "",
                addr == BRAKE_ID ? " <- JRK brake"   : "",
                addr == 0x20     ? " <- PCA9555"      : "",
                addr == DAC_ADDR ? " <- DAC (A0=GND)" : "");
            USER_SERIAL.println(line);
            result += String(line) + "\n";
        }
    }
    if (!found) result += "  No I2C devices found!\n";
    USER_SERIAL.println("=== Scan done ===");
    server.send(200, "text/plain", result);
}

// ============================================================
// setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    String earlyLog = "\n=== ESP32: AAV Controller ===\n";

    pinMode(LED_PIN,       OUTPUT);
    pinMode(RED_LED_PIN,   OUTPUT);
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(BLUE_LED_PIN,  OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(RED_LED_PIN,   LOW);
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(BLUE_LED_PIN,  LOW);

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

    // Flush early log to WebSocket
    int start = 0;
    for (int i = 0; i <= (int)earlyLog.length(); i++) {
        if (i == (int)earlyLog.length() || earlyLog[i] == '\n') {
            String line = earlyLog.substring(start, i);
            line.trim();
            if (line.length() > 0) ws.broadcastTXT(line);
            start = i + 1;
        }
    }

    // Serial1 for ROS
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

    // ---- JRK G2 Initialization ----
    USER_SERIAL.println("Initializing JRK G2 controllers...");

    // Steer JRK
    if (jrkPing(STEER_ID)) {
        jrk_steer_ok = true;
        uint16_t err = jrk_steer.getErrorFlagsHalting();
        jrk_steer.setTarget(JRK_CENTER);
        USER_SERIAL.printf("JRK STEER (0x%02X) OK — centered (%d)\n", STEER_ID, JRK_CENTER);
    } else {
        USER_SERIAL.printf("WARNING: JRK STEER (0x%02X) not found!\n", STEER_ID);
    }

    // Brake JRK
    if (jrkPing(BRAKE_ID)) {
        jrk_brake_ok = true;
        uint16_t err = jrk_steer.getErrorFlagsHalting();
        jrk_brake.setTarget(JRK_MAX);   // engage brake on boot (safe default)
        USER_SERIAL.printf("JRK BRAKE (0x%02X) OK — brake engaged (%d)\n", BRAKE_ID, JRK_MAX);
    } else {
        USER_SERIAL.printf("WARNING: JRK BRAKE (0x%02X) not found!\n", BRAKE_ID);
    }
    // -------------------------------

    USER_SERIAL.println("Ready — SwA: RC_EN | SwC: TOP=Brake, MID=Fwd, LOW=Rev");
}

// ============================================================
// loop
// ============================================================
void loop() {
    static uint8_t buf[13];
    static uint8_t idx = 0;

    server.handleClient();
    ws.loop();
    ibus.loop();

    uint16_t rc_en_raw = ibus.readChannel(RC_EN_CH);
    bool rc_mode = (rc_en_raw > 1500 && rc_en_raw < 2200);

    if (rc_mode) {
        // =============== RC MODE ===============
        uint16_t throttle_rc    = ibus.readChannel(THR_RC_CH);
        uint16_t steering_rc    = ibus.readChannel(STR_RC_CH);
        uint16_t drive_mode_raw = ibus.readChannel(DRIVE_MODE_CH);

        if (throttle_rc    > 100 && throttle_rc    < 2200 &&
            steering_rc    > 100 && steering_rc    < 2200 &&
            drive_mode_raw > 100 && drive_mode_raw < 2200) {

            int drive_mode = 0;
            if (drive_mode_raw < 1200)      drive_mode = 1;  // FWD
            else if (drive_mode_raw > 1800) drive_mode = 2;  // REV
            // else drive_mode = 0 = BRAKE

            // Steering — map RC 1000–2000 → JRK 4095–0 (invert if needed)
            uint16_t jrk_steer_target = (uint16_t)map(steering_rc, 1000, 2000, 4095, 0);
            setSteer(jrk_steer_target);
            current_steering = map(steering_rc, 1000, 2000, -100, 100) / 100.0f;

            float throttle = map(throttle_rc, 1000, 2000, -100, 100) / 100.0f;

            switch (drive_mode) {
                case 0: // BRAKE
                    writeDAC(0);
                    setBrake(JRK_MAX);   // full brake
                    ext_digitalWrite(DIR_PIN, HIGH);
                    current_speed = 0.0;
                    current_accel = -1.0;
                    USER_SERIAL.printf("RC BRAKE: steer=%.2f\n", current_steering);
                    break;

                case 1: // FORWARD
                    setBrake(JRK_MIN);   // release brake
                    if (throttle < -0.05f) {
                        current_speed = throttle;
                        current_accel = 0.0;
                        uint16_t dac_val = (uint16_t)(abs(throttle) * 1023);
                        writeDAC(dac_val);
                        ext_digitalWrite(DIR_PIN, HIGH);
                        USER_SERIAL.printf("RC FWD: steer=%.2f speed=%.2f DAC=%d\n",
                                           current_steering, current_speed, dac_val);
                    } else {
                        writeDAC(0);
                        USER_SERIAL.println("RC FWD: IDLE");
                    }
                    break;

                case 2: // REVERSE
                    setBrake(JRK_MIN);   // release brake
                    if (throttle < -0.05f) {
                        current_speed = throttle;
                        current_accel = 0.0;
                        uint16_t dac_val = (uint16_t)(abs(throttle) * 1023);
                        writeDAC(dac_val);
                        ext_digitalWrite(DIR_PIN, LOW);
                        USER_SERIAL.printf("RC REV: steer=%.2f speed=%.2f DAC=%d\n",
                                           current_steering, current_speed, dac_val);
                    } else {
                        writeDAC(0);
                        USER_SERIAL.println("RC REV: IDLE");
                    }
                    break;
            }
            publishStatus();

        } else {
            jrkSafeStop();
            current_speed = 0.0; current_steering = 0.0; current_accel = 0.0;
            publishStatus();
            USER_SERIAL.println("RC: No valid signal - SAFE STOP");
        }

    } else {
        // =============== ROS2 MODE ===============
        int avail = Serial1.available();
        if (avail > 0) {
            uint8_t temp[64];
            int n = Serial1.readBytes(temp, min(avail, 64));
            for (int i = 0; i < n; i++) {
                buf[idx++] = temp[i];
                if (idx >= 13) {
                    float steering, speed, accel;
                    memcpy(&steering, buf,     4);
                    memcpy(&speed,    buf + 4, 4);
                    memcpy(&accel,    buf + 8, 4);
                    uint8_t checksum = buf[12];
                    uint8_t calc = 0;
                    for (int j = 0; j < 12; j++) calc += buf[j];

                    if (calc == checksum) {
                        current_steering = steering;
                        current_speed    = speed;
                        current_accel    = accel;

                        // Steering
                        uint16_t jrk_steer_target = (uint16_t)map(
                            (long)(steering * 100), -100, 100, 4095, 0);
                        setSteer(jrk_steer_target);

                        // Brake — use accel < 0 as brake signal
                        if (accel < -0.05f) {
                            uint16_t brk = (uint16_t)(abs(accel) * JRK_MAX);
                            setBrake(constrain(brk, JRK_MIN, JRK_MAX));
                        } else {
                            setBrake(JRK_MIN);   // release brake
                        }

                        // Throttle DAC
                        uint16_t dac_val = constrain(
                            (uint16_t)(abs(speed) * 1023), 0, 1023);
                        writeDAC(dac_val);
                        ext_digitalWrite(DIR_PIN, speed >= 0 ? HIGH : LOW);

                        USER_SERIAL.printf("ROS: steer=%.2f speed=%.2f brk=%.2f DAC=%d\n",
                                           steering, speed, accel, dac_val);
                        publishStatus();
                        idx = 0;
                    } else {
                        memmove(buf, buf + 1, 12);
                        idx = 12;
                    }
                }
            }
        }
    }

    // Status broadcast every 200ms
    static unsigned long last_status = 0;
    if (millis() - last_status > 200) {
        publishStatus();
        last_status = millis();
    }

    delay(1);
}
