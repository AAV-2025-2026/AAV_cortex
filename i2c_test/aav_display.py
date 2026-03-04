import smbus2
import time

I2C_BUS     = 1
PICO_ADDR   = 0x42
MAX_RETRIES = 3

bus = None

# ============================================================================
# I2C Scanner
# ============================================================================
def i2c_scan():
    print("\n[I2C] Scanning bus 1...")
    print("     " + "  ".join(f"{i:02X}" for i in range(0, 16)))
    print("     " + "---" * 16)

    found = []
    for row in range(0, 128, 16):
        line = f"{row:02X}: "
        for col in range(16):
            addr = row + col
            try:
                smbus2.SMBus(I2C_BUS).write_quick(addr)
                line += f"{addr:02X} "
                found.append(addr)
            except OSError:
                line += "-- "
        print(line)

    print()
    if found:
        print(f"[I2C] Found {len(found)} device(s): " + ", ".join(f"0x{a:02X}" for a in found))
    else:
        print("[I2C] No devices found.")
    return found

# ============================================================================
# Connect to Pico
# ============================================================================
def connect():
    global bus

    print("\n========================================")
    print("   AAV Display Driver — Pi Side")
    print("========================================")

    found = i2c_scan()

    if PICO_ADDR not in found:
        print(f"\n[ERROR] Pico not found at 0x{PICO_ADDR:02X}")
        print("[HINT]  Check wiring: GP0=SDA, GP1=SCL, GND")
        print("[HINT]  Confirm Pico firmware is flashed and running")
        return False

    print(f"\n[I2C] Pico detected at 0x{PICO_ADDR:02X} ✓")
    print(f"[I2C] Connecting...")
    time.sleep(0.1)

    bus = smbus2.SMBus(I2C_BUS)
    print(f"[I2C] Connected to 0x{PICO_ADDR:02X} on bus {I2C_BUS} ✓")
    print("========================================\n")
    return True

# ============================================================================
# Send Command
# ============================================================================
def send_command(cmd: str):
    data = (cmd + '\n').encode('ascii')
    msg  = smbus2.i2c_msg.write(PICO_ADDR, data)

    for attempt in range(MAX_RETRIES):
        try:
            bus.i2c_rdwr(msg)
            print(f"[TX] {cmd}")
            return True
        except OSError as e:
            print(f"[I2C] Attempt {attempt + 1}/{MAX_RETRIES} failed: {e}")
            time.sleep(0.05)

    print(f"[ERROR] Could not send: '{cmd}'")
    return False

def lcd_row1(text: str): send_command(f"L1:{text[:16]}")
def lcd_row2(text: str): send_command(f"L2:{text[:16]}")

def lcd_update(row1: str, row2: str):
    """Send both rows in one transaction as 'L1:text|L2:text'"""
    cmd = f"L1:{row1[:16]}|{row2[:16]}"
    send_command(cmd)


# ============================================================================
# Main
# ============================================================================
if __name__ == "__main__":
    if not connect():
        exit(1)

    print("[AAV] Sending startup message...")
    lcd_update("  AAV SYSTEMS  ", "   ALL GOOD    ")
    time.sleep(1)

    print("[AAV] Starting telemetry loop (Ctrl+C to stop)\n")

    alt  = 0.0
    batt = 100

    try:
        while True:
            alt  += 0.5
            batt -= 1
            if batt < 0: batt = 100

            lcd_row1(f"Alt:  {alt:.1f} m")
            time.sleep(0.05)
            lcd_row2(f"Batt: {batt}%")

            time.sleep(2)

    except KeyboardInterrupt:
        print("\n[AAV] Stopped by user.")
        lcd_update("  AAV DISPLAY  ", "   OFFLINE     ")
        bus.close()
        print("[I2C] Bus closed.")
