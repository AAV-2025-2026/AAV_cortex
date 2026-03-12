#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "lcd_display.hpp"
#include "aav_telemetry.hpp"

// ====== LED BAR ======
#define LED_0_PIN 28
#define LED_1_PIN 27
#define LED_2_PIN 26
#define LED_3_PIN 22
#define LED_4_PIN 21
#define LED_5_PIN 20
#define LED_6_PIN 19
#define LED_7_PIN 18
#define LED_8_PIN 17
#define LED_9_PIN 16

// ===== KY-040 Encoder =====
#define POT_CLK_PIN 2
#define POT_DT_PIN  3
#define POT_SW_PIN  4

// ===== LCD =====
#define LCD_RS_PIN  7
#define LCD_E_PIN   8
#define LCD_D4_PIN  9
#define LCD_D5_PIN  10
#define LCD_D6_PIN  11
#define LCD_D7_PIN  12

// ===== MAX7219 SPI =====
#define SPI_PORT     spi1
#define SPI_CLK_PIN  14
#define SPI_MOSI_PIN 15
#define SPI_CS_PIN   13

// MAX7219 Registers
#define REG_NOOP        0x00
#define REG_DECODE_MODE 0x09
#define REG_INTENSITY   0x0A
#define REG_SCAN_LIMIT  0x0B
#define REG_SHUTDOWN    0x0C
#define REG_DISPLAYTEST 0x0F

// ===== Button + Buzzer =====
#define BUTTON_PIN      5
#define BUZZER_PIN      6
#define LED_BUILTIN_PIN 25

// ===== I2C Slave =====
#define I2C_PORT       i2c0
#define I2C_SDA_PIN    0
#define I2C_SCL_PIN    1
#define I2C_SLAVE_ADDR 0x42

// ===== Timing =====
#define BTN_DEBOUNCE_US  200000
#define EXT_DEBOUNCE_US  200000
#define PAGE_TIMEOUT_MS  10000
#define BATT_BEEP_MS     30000
#define COMMS_BEEP_MS    10000
#define COMMS_TIMEOUT_MS 3000
#define MATRIX_SWAP_MS   800

// ===== Pages =====
#define PAGE_COUNT 13

// ===== Beep =====
#define BEEP_SHORT 60
#define BEEP_LONG  300
#define BEEP_GAP   80

// ============================================================================
// Globals
// ============================================================================
uint8_t led_pins[10] = {
    LED_0_PIN, LED_1_PIN, LED_2_PIN, LED_3_PIN, LED_4_PIN,
    LED_5_PIN, LED_6_PIN, LED_7_PIN, LED_8_PIN, LED_9_PIN
};

LCDdisplay lcd(LCD_D4_PIN, LCD_D5_PIN, LCD_D6_PIN, LCD_D7_PIN,
               LCD_RS_PIN, LCD_E_PIN, 16, 2);

// Input state
volatile int      enc_delta       = 0;
volatile bool     enc_moved       = false;
volatile bool     btn_pressed     = false;
volatile bool     ext_btn_pressed = false;
volatile uint8_t  last_enc_state  = 0;
volatile uint32_t last_btn_time   = 0;
volatile uint32_t last_ext_time   = 0;

// UI state
int      current_page   = 0;
bool     page_pinned    = false;
uint32_t last_input_ms  = 0;

// Comms watchdog
uint32_t last_i2c_rx_ms  = 0;
uint32_t last_batt_beep  = 0;
uint32_t last_comms_beep = 0;
bool     comms_was_lost  = false;

// LCD buffers
char row0[17] = " AAV SENTINEL  ";
char row1[17] = " Booting...    ";

// Telemetry
static aav_telemetry_t telem  = {};
static uint8_t i2c_rx_buf[sizeof(aav_telemetry_t)];
static int     i2c_rx_idx  = 0;
static bool    i2c_msg_rdy = false;

// Matrix
uint8_t pat_smile[8]   = {0b00111100,0b01000010,0b10100101,0b10000001,
                           0b10100101,0b10011001,0b01000010,0b00111100};
uint8_t pat_cross[8]   = {0b10000001,0b01000010,0b00100100,0b00011000,
                           0b00011000,0b00100100,0b01000010,0b10000001};
uint8_t pat_exclaim[8] = {0b00011000,0b00011000,0b00011000,0b00011000,
                           0b00000000,0b00000000,0b00011000,0b00011000};
uint8_t pat_auto[8]    = {0b00011000,0b00100100,0b01000010,0b11111110,
                           0b10000010,0b10000010,0b10000010,0b00000000};
uint8_t pat_rc[8]      = {0b11111100,0b10000010,0b10000010,0b11111100,
                           0b10010000,0b10001000,0b10000100,0b00000000};
uint8_t pat_fwd[8]     = {0b00011000,0b00111100,0b01111110,0b11111111,
                           0b00011000,0b00011000,0b00011000,0b00000000};
uint8_t pat_rev[8]     = {0b00000000,0b00011000,0b00011000,0b00011000,
                           0b11111111,0b01111110,0b00111100,0b00011000};
uint8_t pat_stop[8]    = {0b00000000,0b00111100,0b01111110,0b01111110,
                           0b01111110,0b01111110,0b00111100,0b00000000};

uint8_t* current_mode_pat = pat_rc;
uint8_t* current_dir_pat  = pat_stop;
bool     show_mode_pat    = true;
uint32_t last_matrix_swap = 0;

// ============================================================================
// BUZZER
// ============================================================================
void beep(uint ms) {
    irq_set_enabled(IO_IRQ_BANK0, false);
    gpio_put(BUZZER_PIN, 1); sleep_ms(ms);
    gpio_put(BUZZER_PIN, 0);
    irq_set_enabled(IO_IRQ_BANK0, true);
}
void beep_short()         { beep(BEEP_SHORT); sleep_ms(BEEP_GAP); }
void beep_long()          { beep(BEEP_LONG);  sleep_ms(BEEP_GAP); }
void beep_boot_start()    { beep_short(); beep_short(); beep_short(); }
void beep_boot_success()  { beep(800); }
void beep_boot_fail()     { for(int i=0;i<4;i++){ beep(50); sleep_ms(50); } }
void beep_i2c_connect()   { beep_short(); beep_short(); }
void beep_i2c_lost()      { beep_long(); beep_short(); beep_short(); }
void beep_page_change()   { beep(30); }
void beep_pinned()        { beep_short(); beep_short(); }
void beep_unpinned()      { beep_short(); }
void beep_home()          { beep(40); sleep_ms(40); beep(40); }
void beep_error()         { beep_long(); sleep_ms(80); beep_long(); }
void beep_estop()         { beep(1500); }
void beep_all_clear()     { beep_short(); beep(600); }
void beep_battery_low()   { beep_short(); beep_long(); beep_short(); }
void beep_comms_timeout() { beep_long(); beep_short(); beep_short(); }

// ============================================================================
// MAX7219
// ============================================================================
void max7219_write(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    gpio_put(SPI_CS_PIN, 0);
    spi_write_blocking(SPI_PORT, buf, 2);
    gpio_put(SPI_CS_PIN, 1);
}
void max7219_init() {
    max7219_write(REG_SHUTDOWN,    0x01);
    max7219_write(REG_DISPLAYTEST, 0x00);
    max7219_write(REG_SCAN_LIMIT,  0x07);
    max7219_write(REG_DECODE_MODE, 0x00);
    max7219_write(REG_INTENSITY,   0x08);
    for (int i = 1; i <= 8; i++) max7219_write(i, 0x00);
}
void matrix_display(uint8_t pattern[8]) {
    for (int row = 0; row < 8; row++) max7219_write(row + 1, pattern[row]);
}
void matrix_tick() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((now - last_matrix_swap) >= MATRIX_SWAP_MS) {
        last_matrix_swap = now;
        show_mode_pat = !show_mode_pat;
        matrix_display(show_mode_pat ? current_mode_pat : current_dir_pat);
    }
}

// ============================================================================
// LED BAR
// ============================================================================
void bar_init() {
    for (int i = 0; i < 10; i++) {
        gpio_init(led_pins[i]);
        gpio_set_dir(led_pins[i], GPIO_OUT);
        gpio_put(led_pins[i], 0);
    }
}
void bar_update(uint16_t sys_status) {
    for (int i = 0; i < 10; i++)
        gpio_put(led_pins[i], (sys_status >> i) & 0x01);
}

// ============================================================================
// LCD Helpers
// ============================================================================
void lcd_refresh() {
    lcd.clear();
    lcd.goto_pos(0, 0); lcd.print(row0);
    lcd.goto_pos(0, 1); lcd.print(row1);
}

const char* drive_mode_str() {
    return (telem.drive_mode == AAV_DRIVE_MODE_AUTONOMOUS) ? "AUTO" : "RC  ";
}
const char* drive_state_str() {
    switch (telem.drive_state) {
        case AAV_DRIVE_FWD:   return "FWD  ";
        case AAV_DRIVE_REV:   return "REV  ";
        case AAV_DRIVE_BRAKE: return "BRAKE";
        default:              return "STOP ";
    }
}
const char* error_str() {
    switch (telem.error_code) {
        case AAV_OK:              return "OK             ";
        case AAV_ERR_IMU:         return "IMU FAIL       ";
        case AAV_ERR_BARO:        return "BARO FAIL      ";
        case AAV_ERR_GPS:         return "GPS NO FIX     ";
        case AAV_ERR_BATTERY_LOW: return "BATTERY LOW    ";
        case AAV_ERR_MOTOR:       return "MOTOR FAIL     ";
        case AAV_ERR_COMMS:       return "COMMS LOST     ";
        case AAV_ERR_ESTOP:       return "!!! E-STOP !!! ";
        default:                  return "UNKNOWN        ";
    }
}

void jrk_decode_rows(uint16_t err, char* r0, char* r1) {
    snprintf(r0, 17, "%s %s %s",
        (err & JRK_ERR_AWAITING) ? "AWR" : "---",
        (err & JRK_ERR_INPUT)    ? "INP" : "---",
        (err & JRK_ERR_FEEDBACK) ? "FBK" : "---");
    snprintf(r1, 17, "%s %s %s",
        (err & JRK_ERR_MOTOR)    ? "MTR" : "---",
        (err & JRK_ERR_OVERCURR) ? "OVR" : "---",
        (err & JRK_ERR_HALTING)  ? "HLT" : "---");
}

// ============================================================================
// Page Renderer
// ============================================================================
void render_page(int page) {
    char r0[17], r1[17];
    char pin = page_pinned ? '*' : ' ';

    switch (page) {
        case 0:  // Overview
            snprintf(r0, 17, "MODE:%-4s      %c", drive_mode_str(), pin);
            snprintf(r1, 17, "STATE:%-5s      ", drive_state_str());
            break;

        case 1:  // Drive Values
            snprintf(r0, 17, "STR:%-6.2f     %c", telem.steering, pin);
            snprintf(r1, 17, "SPD:%.2f AC:%.2f", telem.speed, telem.accel);
            break;

        case 2:  // JRK Steer
            snprintf(r0, 17, "STEER:%-4s     %c",
                telem.jrk_steer_ok ? "OK" : "FAIL", pin);
            snprintf(r1, 17, "ERR: 0x%04X     ", telem.jrk_steer_err);
            break;

        case 3:  // JRK Brake
            snprintf(r0, 17, "BRAKE:%-4s     %c",
                telem.jrk_brake_ok ? "OK" : "FAIL", pin);
            snprintf(r1, 17, "ERR: 0x%04X     ", telem.jrk_brake_err);
            break;

        case 4:  // JRK Steer Decoded
            snprintf(r0, 17, "STEER DECODE  %c", pin);
            jrk_decode_rows(telem.jrk_steer_err, r0, r1);
            break;

        case 5:  // JRK Brake Decoded
            snprintf(r0, 17, "BRAKE DECODE  %c", pin);
            jrk_decode_rows(telem.jrk_brake_err, r0, r1);
            break;

        case 6:  // DAC
            snprintf(r0, 17, "DAC:%-4s       %c",
                telem.dac_ok ? "OK" : "FAIL", pin);
            snprintf(r1, 17, "VAL: %4d/1023  ", telem.dac_val);
            break;

        case 7:  // iBUS Raw
            snprintf(r0, 17, "EN:%-4d TH:%-4d", telem.ibus_en,  telem.ibus_thr);
            snprintf(r1, 17, "ST:%-4d MD:%-4d", telem.ibus_str, telem.ibus_mode);
            break;

        case 8:  // RC Signal
            snprintf(r0, 17, "RC:%-6s       %c",
                telem.rc_active ? "ACTIVE" : "LOST", pin);
            snprintf(r1, 17, "CHNLS:%s        ",
                (telem.ibus_en   > 100 && telem.ibus_thr  > 100 &&
                 telem.ibus_str  > 100 && telem.ibus_mode > 100) ? "OK" : "ERR");
            break;

        case 9: {  // Computers
            uint16_t s = telem.sys_status;
            snprintf(r0, 17, "JET:%s MPI:%s RPI:%s",
                (s & AAV_SYS_JETSON_ORIN) ? "O" : "X",
                (s & AAV_SYS_MOTOR_PI)    ? "O" : "X",
                (s & AAV_SYS_RADAR_PI)    ? "O" : "X");
            snprintf(r1, 17, "UPI:%s  CAM:%s    ",
                (s & AAV_SYS_UI_PI)     ? "O" : "X",
                (s & AAV_SYS_CAMERA_PI) ? "O" : "X");
            break;
        }

        case 10: {  // ROS2 Nodes
            uint16_t s = telem.sys_status;
            snprintf(r0, 17, "IMU:%s RDR:%s LDR:%s",
                (s & AAV_NODE_IMU)   ? "O" : "X",
                (s & AAV_NODE_RADAR) ? "O" : "X",
                (s & AAV_NODE_LIDAR) ? "O" : "X");
            snprintf(r1, 17, "CAM:%s  GPS:%s    ",
                (s & AAV_NODE_CAMERA) ? "O" : "X",
                (s & AAV_NODE_GPS)    ? "O" : "X");
            break;
        }

        case 11:  // GPS
            snprintf(r0, 17, "LAT:%-9.4f  %c", telem.latitude,  pin);
            snprintf(r1, 17, "LON:%-9.4f   ", telem.longitude);
            break;

        case 12:  // AAV Error
            snprintf(r0, 17, "ERR: 0x%02X      %c", telem.error_code, pin);
            snprintf(r1, 17, "%-16s", error_str());
            break;

        default:
            snprintf(r0, 17, "%-16s", "UNKNOWN PAGE   ");
            snprintf(r1, 17, "%-16s", "               ");
            break;
    }

    snprintf(row0, 17, "%-16s", r0);
    snprintf(row1, 17, "%-16s", r1);
    lcd_refresh();
}

// ============================================================================
// Telemetry Processing
// ============================================================================
void process_telemetry() {
    if (telem.magic != AAV_SENTINEL_PKG) {
        printf("[WARN] Bad magic: 0x%02X\n", telem.magic);
        return;
    }
    if (telem.version != AAV_SENTINEL_VERSION) {
        printf("[WARN] Version mismatch: 0x%02X\n", telem.version);
        return;
    }
    uint8_t expected = aav_telem_checksum(&telem);
    if (telem.checksum != expected) {
        printf("[WARN] Checksum fail: got 0x%02X expected 0x%02X\n",
               telem.checksum, expected);
        return;
    }

    last_i2c_rx_ms = to_ms_since_boot(get_absolute_time());

    // LED bar
    bar_update(telem.sys_status);

    // Matrix
    current_mode_pat = (telem.drive_mode == AAV_DRIVE_MODE_AUTONOMOUS)
                       ? pat_auto : pat_rc;
    switch (telem.drive_state) {
        case AAV_DRIVE_FWD:  current_dir_pat = pat_fwd;  break;
        case AAV_DRIVE_REV:  current_dir_pat = pat_rev;  break;
        default:             current_dir_pat = pat_stop; break;
    }

    // Periodic alerts
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (telem.error_code == AAV_ERR_ESTOP) {
        beep_estop();
        matrix_display(pat_cross);
    } else if (telem.error_code != AAV_OK) {
        beep_error();
    }
    if (telem.error_code == AAV_ERR_BATTERY_LOW &&
        (now - last_batt_beep) > BATT_BEEP_MS) {
        beep_battery_low();
        last_batt_beep = now;
    }

    // Refresh display
    render_page(current_page);

    printf("[RX] pg=%d mode=%s state=%s err=0x%02X sys=0b%016b\n",
           current_page, drive_mode_str(), drive_state_str(),
           telem.error_code, telem.sys_status);
}

// ============================================================================
// I2C Slave IRQ
// ============================================================================
static void i2c_slave_irq_handler() {
    i2c_hw_t *hw = i2c_get_hw(I2C_PORT);
    uint32_t intr_stat = hw->intr_stat;

    if (intr_stat & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {
        uint8_t byte = (uint8_t)(hw->data_cmd & 0xFF);
        // Auto-sync on magic byte
        if (i2c_rx_idx == 0 && byte != AAV_SENTINEL_PKG) return;
        if (i2c_rx_idx < (int)sizeof(aav_telemetry_t))
            i2c_rx_buf[i2c_rx_idx++] = byte;
        if (i2c_rx_idx == (int)sizeof(aav_telemetry_t)) {
            memcpy(&telem, i2c_rx_buf, sizeof(aav_telemetry_t));
            i2c_rx_idx  = 0;
            i2c_msg_rdy = true;
        }
    }
    if (intr_stat & I2C_IC_INTR_STAT_R_STOP_DET_BITS) {
        (void)hw->clr_stop_det;
        if (i2c_rx_idx > 0 && i2c_rx_idx < (int)sizeof(aav_telemetry_t)) {
            printf("[WARN] Incomplete pkt (%d/%d), reset\n",
                   i2c_rx_idx, (int)sizeof(aav_telemetry_t));
            i2c_rx_idx = 0;
        }
    }
}

void i2c_slave_init() {
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    i2c_set_slave_mode(I2C_PORT, true, I2C_SLAVE_ADDR);
    i2c_hw_t *hw = i2c_get_hw(I2C_PORT);
    hw->intr_mask = I2C_IC_INTR_MASK_M_RX_FULL_BITS |
                    I2C_IC_INTR_MASK_M_STOP_DET_BITS;
    irq_set_exclusive_handler(I2C0_IRQ, i2c_slave_irq_handler);
    irq_set_enabled(I2C0_IRQ, true);
}

// ============================================================================
// Encoder + Button IRQ
// ============================================================================
void pot_irq(uint gpio, uint32_t events) {
    if (gpio == POT_CLK_PIN || gpio == POT_DT_PIN) {
        uint8_t clk = gpio_get(POT_CLK_PIN);
        uint8_t dt  = gpio_get(POT_DT_PIN);
        uint8_t cur = (clk << 1) | dt;
        uint8_t combined = (last_enc_state << 2) | cur;
        if (combined==0b0001||combined==0b0111||
            combined==0b1110||combined==0b1000)       enc_delta =  1;
        else if (combined==0b0010||combined==0b0100||
                 combined==0b1101||combined==0b1011)  enc_delta = -1;
        last_enc_state = cur;
        enc_moved = true;
    }
    if (gpio == POT_SW_PIN) {
        uint32_t now = time_us_32();
        if ((now - last_btn_time) < BTN_DEBOUNCE_US) return;
        last_btn_time = now; btn_pressed = true;
    }
    if (gpio == BUTTON_PIN) {
        uint32_t now = time_us_32();
        if ((now - last_ext_time) < EXT_DEBOUNCE_US) return;
        last_ext_time = now; ext_btn_pressed = true;
    }
}

// ============================================================================
// Input Handler
// ============================================================================
void handle_input() {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (enc_moved) {
        enc_moved = false;
        last_input_ms = now;
        current_page = (current_page + enc_delta + PAGE_COUNT) % PAGE_COUNT;
        beep_page_change();
        render_page(current_page);
        printf("[UI] Page -> %d\n", current_page);
    }

    if (btn_pressed) {
        btn_pressed = false;
        last_input_ms = now;
        page_pinned = !page_pinned;
        page_pinned ? beep_pinned() : beep_unpinned();
        render_page(current_page);
        printf("[UI] Pin -> %s\n", page_pinned ? "ON" : "OFF");
    }

    if (ext_btn_pressed) {
        ext_btn_pressed = false;
        last_input_ms = now;
        page_pinned  = false;
        current_page = 0;
        beep_home();
        render_page(current_page);
        printf("[UI] Home\n");
    }

    // Auto-return to page 0 on inactivity
    if (!page_pinned && current_page != 0 &&
        (now - last_input_ms) > PAGE_TIMEOUT_MS) {
        current_page = 0;
        render_page(current_page);
        printf("[UI] Timeout -> page 0\n");
    }
}

// ============================================================================
// Comms Watchdog
// ============================================================================
void handle_comms_watchdog() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool lost = (last_i2c_rx_ms > 0) &&
                (now - last_i2c_rx_ms) > COMMS_TIMEOUT_MS;

    if (lost && !comms_was_lost) {
        comms_was_lost = true;
        beep_i2c_lost();
        printf("[WARN] Comms lost\n");
    } else if (!lost && comms_was_lost) {
        comms_was_lost = false;
        beep_i2c_connect();
        beep_all_clear();
        printf("[INFO] Comms restored\n");
    }
    if (lost && (now - last_comms_beep) > COMMS_BEEP_MS) {
        beep_comms_timeout();
        last_comms_beep = now;
    }
}

// ============================================================================
// Boot Sequence
// ============================================================================
void boot_sequence() {
    beep_boot_start();
    snprintf(row0, 17, " AAV SENTINEL  ");
    snprintf(row1, 17, " Initializing. ");
    lcd_refresh();
    matrix_display(pat_smile);
    sleep_ms(600);
    snprintf(row1, 17, " Waiting Pi... ");
    lcd_refresh();
}

// ============================================================================
// main
// ============================================================================
int main() {
    stdio_init_all();
    sleep_ms(2000);

    // LCD
    lcd.init();
    lcd.cursor_off();

    // SPI + MAX7219
    spi_init(SPI_PORT, 1 * 1000 * 1000);
    gpio_set_function(SPI_CLK_PIN,  GPIO_FUNC_SPI);
    gpio_set_function(SPI_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_init(SPI_CS_PIN);
    gpio_set_dir(SPI_CS_PIN, GPIO_OUT);
    gpio_put(SPI_CS_PIN, 1);
    max7219_init();

    // LED bar
    bar_init();

    // Encoder
    gpio_init(POT_CLK_PIN); gpio_pull_up(POT_CLK_PIN);
    gpio_init(POT_DT_PIN);  gpio_pull_up(POT_DT_PIN);
    gpio_init(POT_SW_PIN);  gpio_pull_up(POT_SW_PIN);
    last_enc_state = (gpio_get(POT_CLK_PIN) << 1) | gpio_get(POT_DT_PIN);
    gpio_set_irq_enabled_with_callback(POT_CLK_PIN,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &pot_irq);
    gpio_set_irq_enabled(POT_DT_PIN,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
    gpio_set_irq_enabled(POT_SW_PIN, GPIO_IRQ_EDGE_FALL, true);

    // Ext button
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);
    gpio_set_irq_enabled(BUTTON_PIN, GPIO_IRQ_EDGE_FALL, true);

    // Buzzer
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_put(BUZZER_PIN, 0);

    // LED builtin
    gpio_init(LED_BUILTIN_PIN);
    gpio_set_dir(LED_BUILTIN_PIN, GPIO_OUT);
    gpio_put(LED_BUILTIN_PIN, 1);

    // Boot
    boot_sequence();

    // I2C slave
    i2c_slave_init();

    beep_boot_success();
    last_input_ms = to_ms_since_boot(get_absolute_time());

    printf("[AAV_SENTINEL] Ready — I2C@0x%02X | %d pages | struct=%d bytes\n",
           I2C_SLAVE_ADDR, PAGE_COUNT, (int)sizeof(aav_telemetry_t));

    while (true) {
        if (i2c_msg_rdy) {
            i2c_msg_rdy = false;
            if (!comms_was_lost && last_i2c_rx_ms == 0) beep_i2c_connect();
            process_telemetry();
        }
        handle_input();
        handle_comms_watchdog();
        matrix_tick();
        sleep_ms(5);
    }
}
