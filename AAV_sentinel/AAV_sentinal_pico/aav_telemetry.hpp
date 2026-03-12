#pragma once
#include <stdint.h>

#define AAV_SENTINEL_PKG 0xAA
#define AAV_SENTINEL_VERSION 0x01

//===================== Telemetry Packet Structure ====================

//========== Computer Status (LEDs 0-4) ==========
typedef enum {
    AAV_SYS_JETSON_ORIN = (1 << 0),
    AAV_SYS_MOTOR_PI    = (1 << 1),
    AAV_SYS_RADAR_PI    = (1 << 2),
    AAV_SYS_UI_PI       = (1 << 3),
    AAV_SYS_CAMERA_PI   = (1 << 4) 
} AAV_SystemStatus_t;

// ========== ROS2 Nodes Data (LEDs 5-9) ==========
typedef enum {
    AAV_NODE_IMU      = (1 << 5),
    AAV_NODE_RADAR    = (1 << 6),
    AAV_NODE_LIDAR    = (1 << 7),
    AAV_NODE_CAMERA   = (1 << 8),
    AAV_NODE_GPS      = (1 << 9) 
} AAV_NodeStatus_t;

// ========== Drive Mode (Dot Matrix) ==========
typedef enum {
    AAV_DRIVE_MODE_RC         = 0,
    AAV_DRIVE_MODE_AUTONOMOUS = 1,
} AAV_DriveMode_t;

// ========== Drive Direction (Dot Matrix) ==========
typedef enum {
    AAV_DRIVE_STOP = 0,
    AAV_DRIVE_FWD  = 1,
    AAV_DRIVE_REV  = 2,
    AAV_DRIVE_BRAKE = 3
} AAV_DriveDir_t;

// ===== JRK Error Bit Flags =====
// Matches JRK G2 getErrorFlagsHalting() bitmask
typedef enum {
    JRK_ERR_AWAITING  = (1 << 0),
    JRK_ERR_INPUT     = (1 << 2),
    JRK_ERR_FEEDBACK  = (1 << 4),
    JRK_ERR_MOTOR     = (1 << 5),
    JRK_ERR_OVERCURR  = (1 << 9),
    JRK_ERR_HALTING   = (1 << 11),
} aav_jrk_err_t;

// ========== AAV Error Codes ==========
typedef enum {
    AAV_OK              = 0x00,
    AAV_ERR_IMU         = 0x01,
    AAV_ERR_BARO        = 0x02,
    AAV_ERR_GPS         = 0x03,
    AAV_ERR_BATTERY_LOW = 0x04,
    AAV_ERR_MOTOR       = 0x05,
    AAV_ERR_COMMS       = 0x06,
    AAV_ERR_ESTOP       = 0xFF,
} AAV_ErrorCode_t;


// ============================================================
// Telemetry Packet
// ============================================================
typedef struct __attribute__((packed)) {
    // Header
    uint8_t  magic;             // 0xAA
    uint8_t  version;           // 0x01

    // Page 0 — Overview
    uint8_t  drive_mode;        // aav_drive_mode_t: RC=0 AUTO=1
    uint8_t  drive_state;       // aav_drive_state_t: BRAKE/FWD/REV

    // Page 1 — Drive Values
    float    steering;          // -1.0 to 1.0
    float    speed;             // -1.0 to 1.0
    float    accel;             // -1.0 to 1.0

    // Page 2 — JRK Steer
    uint8_t  jrk_steer_ok;     // 1 = OK, 0 = FAIL
    uint16_t jrk_steer_err;    // raw getErrorFlagsHalting()

    // Page 3 — JRK Brake
    uint8_t  jrk_brake_ok;     // 1 = OK, 0 = FAIL
    uint16_t jrk_brake_err;    // raw getErrorFlagsHalting()

    // Page 6 — DAC
    uint8_t  dac_ok;            // 1 = ACK, 0 = FAIL
    uint16_t dac_val;           // 0–1023

    // Page 7 — iBUS Raw
    uint16_t ibus_en;           // CH5: RC enable
    uint16_t ibus_thr;          // CH2: throttle
    uint16_t ibus_str;          // CH3: steering
    uint16_t ibus_mode;         // CH4: drive mode

    // Page 8 — RC Signal
    uint8_t  rc_active;         // 1 = signal OK, 0 = lost

    // Page 9/10 — Computer + Node Status (LED bitmask)
    uint16_t sys_status;        // aav_computer_t | aav_node_t

    // Page 11 — GPS
    float    latitude;
    float    longitude;

    // Page 12 — AAV Error
    uint8_t  error_code;        // aav_error_t

    // Integrity
    uint8_t  checksum;          // XOR of all bytes before this
} aav_telemetry_t;

// ============================================================
// Checksum
// ============================================================
static inline uint8_t aav_telem_checksum(const aav_telemetry_t* t) {
    const uint8_t* b = (const uint8_t*)t;
    uint8_t csum = 0;
    for (int i = 0; i < (int)(sizeof(aav_telemetry_t) - 1); i++)
        csum ^= b[i];
    return csum;
}