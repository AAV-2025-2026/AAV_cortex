#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>

// WiFi credentials
const char* ssid = "bayshore";
const char* password = "ottawa17";

#define LED_PIN 2  // Builtin LED

WebServer server(80);

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    
    // WiFi Setup
    Serial.println("\n=== ESP32 OTA + Blink Test ===");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println("\n✓ WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // ElegantOTA Setup (simpler!)
    ElegantOTA.begin(&server);
    server.begin();
    
    Serial.println("✓ OTA Ready!");
    Serial.print("Update at: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/update");
    Serial.println("\nBlinking LED...\n");
}

void loop() {
    server.handleClient();  // Handle OTA requests
    ElegantOTA.loop();      // ElegantOTA loop
    
    // Blink LED
    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED ON");
    delay(1000);
    
    digitalWrite(LED_PIN, LOW);
    Serial.println("LED OFF");
    delay(1000);
}
