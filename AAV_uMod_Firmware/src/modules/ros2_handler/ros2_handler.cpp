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
                current_steering = steering;
                current_speed    = speed;
                current_accel    = accel;

                // Steering: -1..+1 → JRK 0..1550 via center=500
                uint16_t jrk_steer_target;
                if (steering <= 0.0f)
                    jrk_steer_target = (uint16_t)(JRK_CENTER + steering * JRK_CENTER);
                else
                    jrk_steer_target = (uint16_t)(JRK_CENTER + steering * (JRK_MAX - JRK_CENTER));
                setSteer(jrk_steer_target);

                // Brake
                if (accel < -0.05f)
                    setBrake(constrain((uint16_t)(abs(accel) * JRK_MAX), (uint16_t)JRK_MIN, (uint16_t)JRK_MAX));
                else
                    setBrake(JRK_MIN);

                // Throttle
                uint16_t dac_val = constrain((uint16_t)(abs(speed) * 1023), 0, 1023);
                writeDACRamped(dac_val);
                ext_digitalWrite(DIR_PIN, speed >= 0 ? HIGH : LOW);

                USER_SERIAL.printf("ROS: steer=%.2f speed=%.2f brk=%.2f DAC=%d\n",
                                   steering, speed, accel, dac_val);
                publishStatus();
                idx = 0;
            } else {
                memmove(buf, buf + 1, 12);
                idx = 12;
            }
        }
    }
}
