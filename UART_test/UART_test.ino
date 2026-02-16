// ESP32 UART2 + LED PWM (Fast Feedback)
#define LED_PIN 2

void setup() {
    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, 16, 17);
    
    ledcSetup(0, 5000, 8);
    ledcAttachPin(LED_PIN, 0);
    ledcWrite(0, 0);
    
    Serial.println("ESP32 UART2 + LED (Fast)");
}

void loop() {
    static uint8_t buf[13];
    static uint8_t idx = 0;
    
    // Read ALL available bytes at once (non-blocking)
    int avail = Serial2.available();
    if (avail > 0) {
        uint8_t temp[64];
        int n = Serial2.readBytes(temp, min(avail, 64));
        
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
                    Serial.printf("✓ s=%.2f v=%.2f\n", steering, speed);
                    
                    // LED PWM
                    ledcWrite(0, constrain((int)(abs(speed) * 255), 0, 255));
                    
                    // INSTANT Echo (no delay)
                    buf[12] = 0;
                    for(int j = 0; j < 12; j++) buf[12] += buf[j];
                    Serial2.write(buf, 13);
                    Serial2.flush();  // Force send NOW
                    
                    idx = 0;
                } else {
                    memmove(buf, buf + 1, 12);
                    idx = 12;
                }
            }
        }
    }
    
    // NO delay() here!
}
