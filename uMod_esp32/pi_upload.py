#!/usr/bin/env python3
# smart_ota.py

import requests
import subprocess
import time
import sys
from pathlib import Path

ESP_IP   = "10.0.0.232"      # ESP32 IP!
ESP_PORT = "/dev/ttyUSB0"    # USB UART fallback

def ota_update(ip, bin_file_str):
    """Try OTA via WiFi first"""
    url = f"http://{ip}/update"
    print(f"OTA → {url}")
    try:
        with open(bin_file_str, 'rb') as f:
            files = {'update': ('firmware.bin', f, 'application/octet-stream')}
            r = requests.post(url, files=files, timeout=60)
            if r.status_code == 200:
                print("OTA SUCCESS! ESP32 rebooting...")
                return True
            else:
                print(f"OTA FAILED: HTTP {r.status_code}")
                print(f"   Response: {r.text}")
    except requests.exceptions.Timeout:
        print("OTA Timeout (ESP32 unreachable?)")
    except Exception as e:
        print(f"OTA Error: {e}")
    return False

def uart_flash(port, bin_file_str):
    """Fallback: UART flash via USB"""
    print(f"\n🔄 UART fallback → {port}")
    cmd = [
        "esptool.py", "--chip", "esp32",
        "--port", port, "--baud", "921600",
        "--before", "default_reset", "--after", "hard_reset",
        "write_flash", "-z", "--verbose",
        "0x1000", bin_file_str
    ]
    print("", " ".join(cmd))
    print("-" * 60)
    result = subprocess.run(cmd, capture_output=False, text=True)
    if result.returncode == 0:
        print("UART SUCCESS!")
        return True
    print(f"UART FAILED (exit {result.returncode})")
    return False

# ===== MAIN =====
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 smart_ota.py firmware.bin")
        sys.exit(1)

    bin_file = Path(sys.argv[1])
    if not bin_file.exists():
        print(f"File not found: {bin_file}")
        sys.exit(1)

    print(f"\n Firmware: {bin_file} ({bin_file.stat().st_size / 1024:.1f} KB)")
    print(f"Target:   {ESP_IP}\n")

    # 1. Try OTA first
    if ota_update(ESP_IP, str(bin_file)):
        sys.exit(0)

    # 2. UART fallback
    print("\n⚠ OTA failed → switching to UART...")
    time.sleep(2)
    uart_flash(ESP_PORT, str(bin_file))
