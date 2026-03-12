#include "rc_handler.h"
#include "globals.h"
#include "websoc_serial/websoc_serial.h"
#include "jrk_g2.h"
#include "dac/dac.h"
#include "web_handler/web_handler.h"

void handleRCMode() {
    uint16_t throttle_rc    = ibus.readChannel(THR_RC_CH);
    uint16_t steering_rc    = ibus.readChannel(STR_RC_CH);
    uint16_t drive_mode_raw = ibus.readChannel(DRIVE_MODE_CH);

    if (throttle_rc    > 100 && throttle_rc    < 2200 &&
        steering_rc    > 100 && steering_rc    < 2200 &&
        drive_mode_raw > 100 && drive_mode_raw < 2200) {

        int drive_mode = 0;
        if      (drive_mode_raw < 1200) drive_mode = 1;  // FWD
        else if (drive_mode_raw > 1800) drive_mode = 2;  // REV

        // Steering: RC 1000–2000 → JRK 0–1550
        uint16_t jrk_steer_target;
        if (steering_rc <= 1500) {
            jrk_steer_target = (uint16_t)map(steering_rc, 1000, 1500, JRK_MAX, JRK_CENTER);
        } else {
            jrk_steer_target = (uint16_t)map(steering_rc, 1500, 2000, JRK_CENTER, 0);
        }
        setSteer(jrk_steer_target);
        current_steering = map(steering_rc, 1000, 2000, 100, -100) / 100.0f;


        float throttle = map(throttle_rc, 1000, 2000, -100, 100) / 100.0f;

        switch (drive_mode) {
            case 0: // BRAKE
                writeDACRamped(0);
                setBrake(JRK_MAX);
                ext_digitalWrite(DIR_PIN, HIGH);
                current_speed = 0.0;
                current_accel = -1.0;
                USER_SERIAL.printf("RC BRAKE: steer=%.2f\n", current_steering);
                break;

            case 1: // FORWARD
                setBrake(JRK_MIN);
                if (throttle < -0.05f) {
                    current_speed = throttle;
                    current_accel = 0.0;
                    uint16_t dac_val = (uint16_t)(abs(throttle) * 1023);
                    writeDACRamped(dac_val);
                    ext_digitalWrite(DIR_PIN, HIGH);
                    USER_SERIAL.printf("RC FWD: steer=%.2f speed=%.2f DAC=%d\n",
                                       current_steering, current_speed, dac_val);
                } else {
                    writeDACRamped(0);   // ← ramp down, not hard cut
                    current_speed = 0.0;
                    USER_SERIAL.println("RC FWD: IDLE");
                }
                break;

            case 2: // REVERSE
                setBrake(JRK_MIN);
                if (throttle < -0.05f) {
                    current_speed = throttle;
                    current_accel = 0.0;
                    uint16_t dac_val = (uint16_t)(abs(throttle) * 1023);
                    writeDACRamped(dac_val);
                    ext_digitalWrite(DIR_PIN, LOW);
                    USER_SERIAL.printf("RC REV: steer=%.2f speed=%.2f DAC=%d\n",
                                       current_steering, current_speed, dac_val);
                } else {
                    writeDACRamped(0);   // ← ramp down, not hard cut
                    current_speed = 0.0;
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
}
