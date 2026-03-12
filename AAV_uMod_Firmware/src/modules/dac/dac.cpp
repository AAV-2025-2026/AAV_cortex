#include "dac.h"
#include "globals.h"
#include "hardware_configs.h"
#include "websoc_serial/websoc_serial.h"

uint32_t current_dac = 0;

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
    int32_t tgt = constrain((int32_t)target, (int32_t)DAC_MIN, (int32_t)DAC_MAX_CAP);

    if (current_dac < tgt)
        current_dac = min((int32_t)(current_dac + ACCEL_STEP), tgt);
    else if (current_dac > tgt)
        current_dac = max((int32_t)(current_dac - DECEL_STEP), tgt);

    current_dac = constrain(current_dac, (int32_t)0, (int32_t)DAC_MAX_CAP);
    writeDAC((uint16_t)current_dac);
    USER_SERIAL.printf("DAC hw write: %d\n", current_dac);
}