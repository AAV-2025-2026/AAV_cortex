// ESP32: ROS2 UART + iBUS RC Override + HTTP OTA
#include <IBusBM.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>  // Built-in, no extra library!

// WiFi credentials
const char* ssid = "your_ssid";
const char* password = "your_password";

#define LED_PIN 2
#define IBUS_RX_PIN 32
#define IBUS_TX_PIN 33
#define IBUS_BAUD 115200

// iBUS channels
#define RC_EN_CH 5
#define DRIVE_MODE_CH 4
#define THR_RC_CH 2
#define STR_RC_CH 3

IBusBM ibus;
WebServer server(80);
HTTPUpdateServer httpUpdater;  // Replaces ElegantOTA

// Current state
float current_steering = 0.0;
float current_speed = 0.0;
float current_accel = 0.0;
unsigned long last_status_time = 0;

void publishStatus() {
    if (millis() - last_status_time < 20) return;
    last_status_time = millis();
    
    uint8_t pkt[13];
    memcpy(pkt, &current_steering, 4);
    memcpy(pkt + 4, &current_speed, 4);
    memcpy(pkt + 8, &current_accel, 4);
    pkt[12] = 0;
    for(int i = 0; i < 12; i++) pkt[12] += pkt[i];
    
    Serial1.write(pkt, 13);
    Serial1.flush();
}

void handleStatus() {
    String html = "<html><body>";
    html += "<h2>ESP32 AAV Status</h2>";
    html += "<p>Steering: " + String(current_steering, 2) + "</p>";
    html += "<p>Speed: "    + String(current_speed,    2) + "</p>";
    html += "<p>Accel: "    + String(current_accel,    2) + "</p>";
    html += "<p>IP: "       + WiFi.localIP().toString() + "</p>";
    html += "<p><a href='/update'>OTA Update</a></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=== ESP32: ROS2 + iBUS RC Override + HTTP OTA ===");
    
    // ROS2 UART on Serial1 (GPIO16 RX, GPIO17 TX)
    Serial1.begin(115200, SERIAL_8N1, 16, 17);
    Serial.println("✓ Serial1 (ROS UART) @ GPIO16/17");
    
    // iBUS on Serial2 (GPIO32 RX, GPIO33 TX)
    pinMode(IBUS_RX_PIN, INPUT_PULLUP);
    Serial2.begin(IBUS_BAUD, SERIAL_8N1, IBUS_RX_PIN, IBUS_TX_PIN);
    delay(100);
    Serial.println("✓ Serial2 (iBUS) @ GPIO32/33");
    
    // Initialize IBusBM
    ibus.begin(Serial2, IBUSBM_NOTIMER);
    Serial.println("✓ IBusBM initialized");
    
    // LED PWM
    ledcSetup(0, 5000, 8);
    ledcAttachPin(LED_PIN, 0);
    ledcWrite(0, 0);
    Serial.println("✓ LED PWM ready");
    
    // ========== HTTP OTA SETUP ==========
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("WiFi connecting");
    
    unsigned long wifi_start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifi_start < 10000) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✓ WiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        
        server.on("/",       HTTP_GET, handleStatus);
        server.on("/status", HTTP_GET, handleStatus);
        
        httpUpdater.setup(&server);  // Mounts /update endpoint
        server.begin();
        
        Serial.print("✓ Status: http://");
        Serial.print(WiFi.localIP());
        Serial.println("/");
        Serial.print("✓ OTA:    http://");
        Serial.print(WiFi.localIP());
        Serial.println("/update");
    } else {
        Serial.println("\n⚠ WiFi failed - continuing without OTA");
    }
    // ====================================
    
    Serial.println("\nSwA: RC_EN | SwC: TOP=Brake, MID=Fwd, LOW=Rev\n");
}

void loop() {
    static uint8_t buf[13];
    static uint8_t idx = 0;
    
    // ========== OTA HANDLER ==========
    server.handleClient();  // Handles /update + /status (no ElegantOTA.loop() needed!)
    // =================================
    
    ibus.loop();
    
    uint16_t rc_en_raw = ibus.readChannel(RC_EN_CH);
    bool rc_mode = (rc_en_raw > 1500 && rc_en_raw < 2200);
    
    if (rc_mode) {
        // =============== RC MODE ===============
        uint16_t throttle_rc    = ibus.readChannel(THR_RC_CH);
        uint16_t steering_rc    = ibus.readChannel(STR_RC_CH);
        uint16_t drive_mode_raw = ibus.readChannel(DRIVE_MODE_CH);
        
        current_speed    = 0.0;
        current_steering = 0.0;
        current_accel    = 0.0;
        ledcWrite(0, 0);
        
        if (throttle_rc    > 100 && throttle_rc    < 2200 && 
            steering_rc    > 100 && steering_rc    < 2200 && 
            drive_mode_raw > 100 && drive_mode_raw < 2200) {
            
            int drive_mode = 0;
            if (drive_mode_raw < 1200) {
                drive_mode = 1;
            } else if (drive_mode_raw > 1800) {
                drive_mode = 2;
            }
            
            current_steering = map(steering_rc, 1000, 2000, -100, 100) / 100.0;
            float throttle   = map(throttle_rc, 1000, 2000, -100, 100) / 100.0;
            
            switch(drive_mode) {
                case 0:  // BRAKE
                    current_speed = 0.0;
                    current_accel = -1.0;
                    ledcWrite(0, 0);
                    Serial.printf("RC BRAKE: steer=%.2f\n", current_steering);
                    break;
                    
                case 1:  // FORWARD
                    if (throttle < -0.05) {
                        current_speed = throttle;
                        current_accel = 0.0;
                        ledcWrite(0, (int)(abs(throttle) * 255));
                        Serial.printf("RC FWD: steer=%.2f speed=%.2f PWM=%d\n", 
                                     current_steering, current_speed, (int)(abs(throttle) * 255));
                    } else {
                        Serial.println("RC FWD: IDLE");
                    }
                    break;
                    
                case 2:  // REVERSE
                    if (throttle < -0.05) {
                        current_speed = throttle;
                        current_accel = 0.0;
                        ledcWrite(0, (int)(abs(throttle) * 255));
                        Serial.printf("RC REV: steer=%.2f speed=%.2f PWM=%d\n", 
                                     current_steering, current_speed, (int)(abs(throttle) * 255));
                    } else {
                        Serial.println("RC REV: IDLE");
                    }
                    break;
            }
            
            publishStatus();
            
        } else {
            publishStatus();
            Serial.println("RC: No valid signal - STOPPED");
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
                    for(int j = 0; j < 12; j++) calc += buf[j];
                    
                    if (calc == checksum) {
                        current_steering = steering;
                        current_speed    = speed;
                        current_accel    = accel;
                        
                        int pwm = constrain((int)(abs(speed) * 255), 0, 255);
                        ledcWrite(0, pwm);
                        
                        Serial.printf("ROS: s=%.2f v=%.2f PWM=%d\n", 
                                     steering, speed, pwm);
                        
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
    
    delay(1);
}
