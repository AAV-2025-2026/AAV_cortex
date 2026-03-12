#include "ros2_handler.h"
#include "globals.h"
#include "websoc_serial/websoc_serial.h"
#include "jrk_g2/jrk_g2.h"
#include "dac/dac.h"
#include "web_handler/web_handler.h"

void handleROS2Messages(uint8_t* buf, uint8_t& idx) {
    int avail = Serial1.available();
    if (avail <= 0) return;

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
            if (fabsf(steering) <= 1.0f && fabsf(speed) <= 1.0f && fabsf(accel) <= 1.0f) {
                current_steering = steering;
                current_speed    = speed;
                current_accel    = accel;

                // Steering
                uint16_t jrk_steer_target;
                if (steering <= 0.0f)
                    jrk_steer_target = (uint16_t)(JRK_CENTER + steering * JRK_CENTER);
                else
                    jrk_steer_target = (uint16_t)(JRK_CENTER + steering * (JRK_MAX - JRK_CENTER));
                setSteer(jrk_steer_target);

                // Brake — accel < -0.05 means brake demand
                if (accel < -0.05f) {
                    setBrake(constrain((uint16_t)(fabsf(accel) * JRK_MAX),
                                    (uint16_t)JRK_MIN, (uint16_t)JRK_MAX));
                    writeDAC(0);        // hard cut — bypass ramp
                    current_dac = 0;    // reset ramp state so it doesn't ramp back up
                    USER_SERIAL.println("ROS: BRAKE ENGAGED — DAC cut");   // ← kill throttle when braking
                    idx = 0;
                    return;
                } else {
                    setBrake(JRK_MIN);
                }
                
                USER_SERIAL.printf("DBG: spd=%.3f accel=%.3f dac_will=%d\n",
                   speed, accel, (fabsf(speed) < 0.03f) ? 0 : (uint16_t)(fabsf(speed)*DAC_MAX_CAP));

                // Throttle — deadzone: ignore speeds below 3%
                if (fabsf(speed) < 0.03f) {
                    writeDACRamped(0);
                } else {
                    ext_digitalWrite(DIR_PIN, speed >= 0 ? HIGH : LOW);
                    delay(1);
                    uint16_t dac_val = constrain((uint16_t)(fabsf(speed) * DAC_MAX_CAP), 0, DAC_MAX_CAP);
                    writeDACRamped(dac_val);
                }

                USER_SERIAL.printf("ROS: steer=%.2f speed=%.2f brk=%.2f DAC=%d\n",
                                steering, speed, accel,
                                (uint16_t)(fabsf(speed) * DAC_MAX_CAP));
                publishStatus();

                } else {
                    USER_SERIAL.println("ROS: packet rejected — out of range");
                    writeDACRamped(0);
                }
                idx = 0;

            } else {
                memmove(buf, buf + 1, 12);
                idx = 12;
            }
        }
    }
}

