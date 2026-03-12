#include <Arduino.h>
#include <JrkG2.h>
#include "globals.h"
#include "hardware_configs.h"
#include "dac/dac.h"
#include "websoc_serial/websoc_serial.h"
#include "web_handler/web_handler.h"

bool jrkPing(uint8_t addr) {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

void setSteer(uint16_t target) {
    if (!jrk_steer_ok) return;
    target = constrain(target, JRK_MIN, JRK_MAX);
    jrk_steer.setTarget(target);

    uint16_t err = jrk_steer.getErrorFlagsHalting();
    if (err) USER_SERIAL.printf("JRK STEER ERR: 0x%04X\n", err);
}

void setBrake(uint16_t target) {
    if (!jrk_brake_ok) return;
    target = constrain(target, JRK_MIN, JRK_MAX);
    jrk_brake.setTarget(target);

    uint16_t err = jrk_brake.getErrorFlagsHalting();
    if (err) USER_SERIAL.printf("JRK BRAKE ERR: 0x%04X\n", err);
}

void jrkSafeStop() {
    setSteer(JRK_CENTER);
    setBrake(JRK_MAX);
    writeDAC(0);
    USER_SERIAL.println("JRK SAFE STOP");
}