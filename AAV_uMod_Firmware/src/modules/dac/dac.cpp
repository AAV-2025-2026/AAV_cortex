#include "dac.h"
#include "globals.h"
#include "hardware_configs.h"
#include "websoc_serial/websoc_serial.h"

static uint16_t current_dac;

void writeDAC(uint16_t value) {
    value &= 0x3FF;
    uint8_t upperByte = (value >> 6) & 0x0F;
    uint8_t lowerByte = (value << 2) & 0xFC;
    Wire.beginTransmission(DAC_ADDR);
    Wire.write(upperByte);
    Wire.write(lowerByte);
    if (Wire.endTransmission() != 0)
        USER_SERIAL.println("ERROR: DAC did not acknowledge!");
}

bool detectDAC() {
    Wire.beginTransmission(DAC_ADDR);
    return (Wire.endTransmission() == 0);
}

void ext_digitalWrite(uint8_t pin, bool value) {
    ioex.write(
        static_cast<PCA95x5::Port::Port>(pin),
        value ? PCA95x5::Level::H : PCA95x5::Level::L
    );
}

void writeDACRamped(uint16_t target) {
    target = constrain(target, DAC_MIN, DAC_MAX_CAP);  // hard cap

    if (target > current_dac) {
        current_dac = min((uint16_t)(current_dac + ACCEL_STEP), target);
    } else if (target < current_dac) {
        current_dac = max((uint16_t)(current_dac - DECEL_STEP), target);
    }
    writeDAC(current_dac);
}