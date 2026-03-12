#pragma once

#include <Arduino.h>

void writeDAC(uint16_t value);
bool detectDAC();
void ext_digitalWrite(uint8_t pin, bool value);
void writeDACRamped(uint16_t target);
