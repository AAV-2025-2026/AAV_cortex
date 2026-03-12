#pragma once

#include <Arduino.h>
#include <WebSocketsServer.h>

extern WebSocketsServer ws;

class WSSerial : public Print {
public:
    size_t write(uint8_t c) override {
        Serial.write(c);
        _buf += (char)c;
        if (c == '\n') {
            _buf.trim();
            if (_buf.length() > 0) ws.broadcastTXT(_buf);
            _buf = "";
        }
        return 1;
    }
    size_t write(const uint8_t* buffer, size_t size) override {
        for (size_t i = 0; i < size; i++) write(buffer[i]);
        return size;
    }
private:
    String _buf;
};

extern WSSerial WSerial;
#define USER_SERIAL WSerial
