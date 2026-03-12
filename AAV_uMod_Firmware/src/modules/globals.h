#pragma once

// ── Framework ─────────────────────────────────────────────
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include <Wire.h>

// ── Libraries ─────────────────────────────────────────────
#include <IBusBM.h>
#include <JrkG2.h>
#include <WebSocketsServer.h>
#include <PCA95x5.h>

// ── Project headers ───────────────────────────────────────
#include "hardware_configs.h"

// ── Global object externs ─────────────────────────────────
extern WebServer        server;
extern HTTPUpdateServer httpUpdater;
extern WebSocketsServer ws;
extern IBusBM           ibus;
extern PCA9555          ioex;
extern JrkG2I2C         jrk_steer;
extern JrkG2I2C         jrk_brake;

extern float current_steering;
extern float current_speed;
extern float current_accel;
extern bool  jrk_steer_ok;
extern bool  jrk_brake_ok;
