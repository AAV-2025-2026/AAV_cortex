#pragma once

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

// JRK target range: 400-3500 in general (used for Brakes)
#define JRK_MIN        400
#define JRK_CENTER     1950
#define JRK_MAX        3500

// Our steering configuration maps ~500 to full left, ~1550 to full right, and ~1000 to center
#define JRK_STEER_CENTER     500
#define JRK_STEER_MIN        0
#define JRK_STEER_MAX        1550

// ============================================================
// WiFi credentials
// ============================================================
extern const char* ssid;
extern const char* password;

// ============================================================
// DAC configuration
// ============================================================
#define ACCEL_STEP    6
#define DECEL_STEP    12
#define DAC_MAX_CAP   700    // tune: ~68% of 1023 full scale
#define DAC_MIN       0

// === GLOBALS ===
extern WebServer        server;
extern HTTPUpdateServer httpUpdater;
extern WebSocketsServer ws;
extern IBusBM           ibus;
extern PCA9555          ioex;
extern JrkG2I2C         jrk_steer;
extern JrkG2I2C         jrk_brake;

extern float current_steering, current_speed, current_accel;
extern bool  jrk_steer_ok, jrk_brake_ok;