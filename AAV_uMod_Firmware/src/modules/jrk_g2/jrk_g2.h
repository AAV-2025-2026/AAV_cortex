#pragma once

#include <Arduino.h>

bool     jrkPing(uint8_t addr);
void     setSteer(uint16_t target);
void     setBrake(uint16_t target);
void     jrkSafeStop();
