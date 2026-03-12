#include "web_handler.h"
#include "globals.h"
#include "websoc_serial/websoc_serial.h"

void publishStatus() {
    char buf[80];
    snprintf(buf, sizeof(buf), "STATUS:%.2f,%.2f,%.2f | STR:%s BRK:%s",
             current_steering, current_speed, current_accel,
             jrk_steer_ok ? "OK" : "FAIL",
             jrk_brake_ok ? "OK" : "FAIL");
    USER_SERIAL.println(buf);
}

void handleStatus() {
    String html = R"(<!DOCTYPE html>
<html>
<head>
  <title>AAV Controller</title>
  <style>
    body { font-family: sans-serif; padding: 20px; background: #1a1a1a; color: #eee; }
    h2   { color: #4fc3f7; }
    h3   { color: #81c784; }
    .val { font-weight: bold; color: #fff; }
    .ok  { color: #00e676; }
    .err { color: #ff5252; }
    #log {
      font-family: monospace; font-size: 13px;
      background: #111; color: #00e676;
      height: 350px; overflow-y: auto;
      padding: 10px; border: 1px solid #333; border-radius: 4px;
    }
    a { color: #4fc3f7; }
  </style>
</head>
<body>
  <h2>AAV Controller</h2>
  <p>Steering: <span class='val' id='steer'>--</span></p>
  <p>Speed:    <span class='val' id='speed'>--</span></p>
  <p>Accel:    <span class='val' id='accel'>--</span></p>
  <p>JRK Steer: <span class='val' id='jrkstr'>--</span> | JRK Brake: <span class='val' id='jrkbrk'>--</span></p>
  <hr>
  <h3>Serial Log</h3>
  <div id='log'></div>
  <br><a href='/update'>OTA Update</a> | <a href='/scan'>I2C Scan</a>
  <script>
    var ws = new WebSocket('ws://' + location.hostname + ':81/');
    ws.onmessage = function(e) {
      var log = document.getElementById('log');
      var line = document.createElement('div');
      line.textContent = e.data;
      log.appendChild(line);
      log.scrollTop = log.scrollHeight;
      if (e.data.startsWith('STATUS:')) {
        var main = e.data.replace('STATUS:','').split(' | ');
        var parts = main[0].split(',');
        document.getElementById('steer').textContent = parts[0];
        document.getElementById('speed').textContent = parts[1];
        document.getElementById('accel').textContent = parts[2];
        if (main[1]) {
          var jrk = main[1].split(' ');
          var strEl = document.getElementById('jrkstr');
          var brkEl = document.getElementById('jrkbrk');
          strEl.textContent = jrk[0].replace('STR:','');
          brkEl.textContent = jrk[1].replace('BRK:','');
          strEl.className = strEl.textContent === 'OK' ? 'val ok' : 'val err';
          brkEl.className = brkEl.textContent === 'OK' ? 'val ok' : 'val err';
        }
      }
    };
    ws.onclose = function() {
      document.getElementById('log').innerHTML +=
        '<div style="color:red">-- disconnected --</div>';
    };
  </script>
</body>
</html>)";
    server.send(200, "text/html", html);
}

void handleScan() {
    String result = "=== I2C Scan ===\n";
    bool found = false;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            found = true;
            char line[60];
            snprintf(line, sizeof(line), "  Found: 0x%02X%s%s%s%s", addr,
                addr == STEER_ID ? " <- JRK steer"   : "",
                addr == BRAKE_ID ? " <- JRK brake"   : "",
                addr == 0x20     ? " <- PCA9555"      : "",
                addr == DAC_ADDR ? " <- DAC (A0=GND)" : "");
            USER_SERIAL.println(line);
            result += String(line) + "\n";
        }
    }
    if (!found) result += "  No I2C devices found!\n";
    USER_SERIAL.println("=== Scan done ===");
    server.send(200, "text/plain", result);
}
