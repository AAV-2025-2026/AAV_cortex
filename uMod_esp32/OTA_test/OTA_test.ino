#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>   // <-- built‑in HTTP OTA

// WiFi credentials
const char* ssid     = "your_ssid";
const char* password = "your_password";

#define LED_PIN 2  // Builtin LED

WebServer        server(80);
HTTPUpdateServer httpUpdater;   // <-- new

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);

    Serial.println("\n=== ESP32 HTTPUpdate + Blink Test ===");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");

    while (WiFi.status() != WL_CONNECTED) {   // you can add timeout if you want
        delay(500);
        Serial.print(".");
    }

    Serial.println("\n✓ WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Root page just to confirm it is alive
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/plain", "ESP32 HTTPUpdate online");
    });

    // HTTP OTA setup  -> endpoint: /update
    httpUpdater.setup(&server);      // <-- replaces ElegantOTA.begin(&server)
    server.begin();

    Serial.println("✓ HTTPUpdateServer Ready!");
    Serial.print("Update at: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/update");
    Serial.println("\nBlinking LED...\n");
}

void loop() {
    server.handleClient();  // Handle HTTP + update requests

    // Blink LED
    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED ON");
    delay(1000);

    digitalWrite(LED_PIN, LOW);
    Serial.println("LED OFF");
    delay(1000);
}
