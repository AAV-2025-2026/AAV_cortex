// ESP32: ROS2 UART + iBUS RC Override (Using Working iBUS Init)
#include <IBusBM.h>

#define LED_PIN 2
#define IBUS_RX_PIN 32
#define IBUS_TX_PIN 33
#define IBUS_BAUD 115200

// iBUS channels
#define RC_EN_CH 5      // SwA: RC enable (Ch6)
#define DRIVE_MODE_CH 4 // SwC: Drive mode (Ch5)
#define THR_RC_CH 2     // Throttle (Ch3)
#define STR_RC_CH 3     // Steering (Ch4)

IBusBM ibus;

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
    
    Serial1.write(pkt, 13);  // Serial1 for ROS (GPIO16/17)
    Serial1.flush();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=== ESP32: ROS2 + iBUS RC Override ===");
    
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
    
    Serial.println("\nSwA: RC_EN | SwC: TOP=Brake, MID=Fwd, LOW=Rev\n");
}

void loop() {
    static uint8_t buf[13];
    static uint8_t idx = 0;
    
    ibus.loop();  // MUST call every loop
    
    // Read RC_EN switch (SwA on Ch6)
    uint16_t rc_en_raw = ibus.readChannel(RC_EN_CH);
    bool rc_mode = (rc_en_raw > 1500 && rc_en_raw < 2200);
    
    if (rc_mode) {
        // =============== RC MODE ===============
        uint16_t throttle_rc = ibus.readChannel(THR_RC_CH);
        uint16_t steering_rc = ibus.readChannel(STR_RC_CH);
        uint16_t drive_mode_raw = ibus.readChannel(DRIVE_MODE_CH);
        
        // Default STOP
        current_speed = 0.0;
        current_steering = 0.0;
        current_accel = 0.0;
        ledcWrite(0, 0);
        
        if (throttle_rc > 100 && throttle_rc < 2200 && 
            steering_rc > 100 && steering_rc < 2200 && 
            drive_mode_raw > 100 && drive_mode_raw < 2200) {
            
            // Decode SwC position
            int drive_mode = 1;  // Default MID
            if (drive_mode_raw < 1200) {
                drive_mode = 0;  // TOP = Brake
            } else if (drive_mode_raw > 1800) {
                drive_mode = 2;  // LOW = Reverse
            }
            
            // Map steering -1 to +1
            current_steering = map(steering_rc, 1000, 2000, -100, 100) / 100.0;
            
            // Map throttle (centered at 1500)
            float throttle = map(throttle_rc, 1000, 2000, -100, 100) / 100.0;
            
            switch(drive_mode) {
                case 0:  // BRAKE
                    current_speed = 0.0;
                    current_accel = -1.0;
                    ledcWrite(0, 0);
                    Serial.printf("RC BRAKE: steer=%.2f\n", current_steering);
                    break;
                    
                case 1:  // FORWARD
                    if (throttle < -0.05) {  // Deadzone
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
                    if (throttle < -0.05) {  // Deadzone
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
                    memcpy(&steering, buf, 4);
                    memcpy(&speed, buf + 4, 4);
                    memcpy(&accel, buf + 8, 4);
                    uint8_t checksum = buf[12];
                    
                    uint8_t calc = 0;
                    for(int j = 0; j < 12; j++) calc += buf[j];
                    
                    if (calc == checksum) {
                        current_steering = steering;
                        current_speed = speed;
                        current_accel = accel;
                        
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
