#include <IBusBM.h>

#define IBUS_RX_PIN 32
#define IBUS_TX_PIN 33  // Not used but define anyway
#define IBUS_BAUD 115200
#define THROTTLE_PIN LED_BUILTIN

IBusBM ibus;
unsigned long lastPrint = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);
  pinMode(IBUS_RX_PIN, INPUT_PULLUP);
  pinMode(THROTTLE_PIN, OUTPUT);
  
  Serial.println("\n=== iBUS DEBUG (Explicit Serial2 Init) ===");
  
  // EXPLICIT Serial2 initialization BEFORE IBusBM
  Serial2.begin(IBUS_BAUD, SERIAL_8N1, IBUS_RX_PIN, IBUS_TX_PIN);
  delay(100);
  Serial.println("✓ Serial2 initialized at 115200 baud");
  
  // Then initialize IBusBM (it should use already-configured Serial2)
  ibus.begin(Serial2, IBUSBM_NOTIMER);
  Serial.println("✓ IBusBM initialized");
  Serial.println("Waiting for iBUS packets...\n");
}

void loop() {
  ibus.loop();  // MUST call every loop
  
  if (millis() - lastPrint > 200) {  // Print every 200ms
    Serial.println("=== Channel Status ===");
    
    for (int i = 0; i < 6; i++) {
      uint16_t ch = ibus.readChannel(i);
      Serial.printf("Ch%d: %4d", i, ch);
      
      if (ch > 100 && ch < 2200) {
        Serial.print(" ✓");
      } else {
        Serial.print(" ❌");
      }
      Serial.println();
    }
    
    uint16_t throt = ibus.readChannel(2);
    uint16_t steer = ibus.readChannel(3);
    
    Serial.println("\n--- Throttle/Steering ---");
    Serial.printf("Throttle: %d\n", throt);
    Serial.printf("Steering: %d\n", steer);
    
    if (throt > 100 && throt < 2200) {
      int pwm = map(throt, 1000, 2000, 255, 0);
      analogWrite(THROTTLE_PIN, pwm);
      float percent = ((float)(2000 - throt) / 1000) * 100;
      Serial.printf("PWM:%3d (%.0f%%)\n", pwm, percent);
    } else {
      Serial.println("⚠️  No valid throttle signal");
      Serial.println("Check: RX wired to GPIO32? TX powered? Bound?");
    }
    
    Serial.println();
    lastPrint = millis();
  }
}
