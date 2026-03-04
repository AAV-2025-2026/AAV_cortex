#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/pwm.h"
#include "lcd_display.hpp"

// ====== 2510SR-1 LED BAR ======
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

// ===== KY-040 =====
#define POT_CLK_PIN 2
#define POT_DT_PIN  3
#define POT_SW_PIN  4

// ===== LCD =====
#define LCD_RS_PIN        7
#define LCD_E_PIN         8
#define LCD_D4_PIN        9
#define LCD_D5_PIN        10
#define LCD_D6_PIN        11
#define LCD_D7_PIN        12

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
#define BUTTON_PIN 5
#define BUZZER_PIN 6

// ===== LED_BUILTIN (GPIO 25) =====
#define LED_BUILTIN_PIN 25

#define BTN_DEBOUNCE_US     200000
#define EXT_BTN_DEBOUNCE_US 200000

// ============================================================================

uint8_t led_pins[10] = {
    LED_0_PIN, LED_1_PIN, LED_2_PIN, LED_3_PIN, LED_4_PIN,
    LED_5_PIN, LED_6_PIN, LED_7_PIN, LED_8_PIN, LED_9_PIN
};

LCDdisplay lcd(LCD_D4_PIN, LCD_D5_PIN, LCD_D6_PIN, LCD_D7_PIN,
               LCD_RS_PIN, LCD_E_PIN, 16, 2);

volatile int      count             = 5;
volatile bool     enc_moved         = false;
volatile bool     btn_pressed       = false;
volatile bool     ext_btn_pressed   = false;
volatile uint8_t  last_state        = 0;
volatile uint32_t last_btn_time     = 0;
volatile uint32_t last_ext_btn_time = 0;

uint pwm_slice;

char row0[17] = "  Hello, World! ";
char row1[17] = "   Pico Ready   ";

// Built-in patterns
uint8_t pat_smile[8] = {
    0b00111100, 0b01000010, 0b10100101, 0b10000001,
    0b10100101, 0b10011001, 0b01000010, 0b00111100
};
uint8_t pat_checker[8] = {
    0b10101010, 0b01010101, 0b10101010, 0b01010101,
    0b10101010, 0b01010101, 0b10101010, 0b01010101
};
uint8_t pat_heart[8] = {
    0b00000000, 0b01100110, 0b11111111, 0b11111111,
    0b01111110, 0b00111100, 0b00011000, 0b00000000
};
uint8_t pat_current[8];

// ============================================================================
// BUZZER (active buzzer — just GPIO HIGH/LOW)
// ============================================================================
void beep(uint duration_ms) {
    irq_set_enabled(IO_IRQ_BANK0, false);
    gpio_put(BUZZER_PIN, 1);
    sleep_ms(duration_ms);
    gpio_put(BUZZER_PIN, 0);
    irq_set_enabled(IO_IRQ_BANK0, true);
}

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
    for (int row = 0; row < 8; row++)
        max7219_write(row + 1, pattern[row]);
    memcpy(pat_current, pattern, 8);
}

void matrix_clear() {
    for (int i = 1; i <= 8; i++) max7219_write(i, 0x00);
    memset(pat_current, 0, 8);
}

void matrix_set_brightness(uint8_t level) {
    max7219_write(REG_INTENSITY, level & 0x0F);
}

// ============================================================================
// LED BAR + LCD
// ============================================================================
void bar_set(int level) {
    for (int i = 0; i < 10; i++)
        gpio_put(led_pins[i], i < level);
}

void lcd_refresh() {
    lcd.clear();
    lcd.goto_pos(0, 0); lcd.print(row0);
    lcd.goto_pos(0, 1); lcd.print(row1);
}

// ============================================================================
// Serial Protocol:
//   L1:text              → LCD row 1
//   L2:text              → LCD row 2
//   M:smile              → dot matrix: smile
//   M:heart              → dot matrix: heart
//   M:checker            → dot matrix: checker
//   M:clear              → dot matrix: clear
//   M:raw:XXXXXXXXXXXXXXXX → dot matrix: 8 hex bytes
//   MB:0-15              → dot matrix brightness
//   BZ:ms                → trigger buzzer for N milliseconds
// ============================================================================
void handle_serial() {
    static char buf[40];
    static int  idx = 0;

    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) return;

    if (c == '\n' || c == '\r') {
        buf[idx] = '\0';
        idx = 0;

        if (strncmp(buf, "L1:", 3) == 0) {
            snprintf(row0, sizeof(row0), "%-16s", buf + 3);
            lcd_refresh();
            printf("LCD row 0 → \"%s\"\n", row0);

        } else if (strncmp(buf, "L2:", 3) == 0) {
            snprintf(row1, sizeof(row1), "%-16s", buf + 3);
            lcd_refresh();
            printf("LCD row 1 → \"%s\"\n", row1);

        } else if (strcmp(buf, "M:smile") == 0) {
            matrix_display(pat_smile);
            printf("Matrix → smile\n");

        } else if (strcmp(buf, "M:heart") == 0) {
            matrix_display(pat_heart);
            printf("Matrix → heart\n");

        } else if (strcmp(buf, "M:checker") == 0) {
            matrix_display(pat_checker);
            printf("Matrix → checker\n");

        } else if (strcmp(buf, "M:clear") == 0) {
            matrix_clear();
            printf("Matrix → cleared\n");

        } else if (strncmp(buf, "M:raw:", 6) == 0) {
            const char* hex = buf + 6;
            if (strlen(hex) == 16) {
                uint8_t pat[8];
                bool valid = true;
                for (int i = 0; i < 8; i++) {
                    char byte_str[3] = {hex[i*2], hex[i*2+1], '\0'};
                    char* end;
                    pat[i] = (uint8_t)strtol(byte_str, &end, 16);
                    if (*end != '\0') { valid = false; break; }
                }
                if (valid) {
                    matrix_display(pat);
                    printf("Matrix → raw pattern applied\n");
                } else {
                    printf("Invalid hex. Use 16 hex chars e.g. M:raw:3C4281818181423C\n");
                }
            } else {
                printf("M:raw needs exactly 16 hex chars\n");
            }

        } else if (strncmp(buf, "MB:", 3) == 0) {
            int level = atoi(buf + 3);
            if (level >= 0 && level <= 15) {
                matrix_set_brightness((uint8_t)level);
                printf("Matrix brightness → %d\n", level);
            } else {
                printf("MB: range is 0-15\n");
            }

        } else if (strncmp(buf, "BZ:", 3) == 0) {
            int ms = atoi(buf + 3);
            if (ms > 0 && ms <= 5000) {
                beep(ms);
                printf("Buzzer → %dms\n", ms);
            } else {
                printf("BZ: range is 1-5000ms\n");
            }

        } else if (strlen(buf) > 0) {
            printf("Commands:\n");
            printf("  L1:text | L2:text\n");
            printf("  M:smile | M:heart | M:checker | M:clear\n");
            printf("  M:raw:XXXXXXXXXXXXXXXX  (16 hex chars)\n");
            printf("  MB:0-15  (matrix brightness)\n");
            printf("  BZ:ms    (buzzer duration 1-5000ms)\n");
        }

    } else {
        if (idx < (int)(sizeof(buf) - 1))
            buf[idx++] = (char)c;
    }
}

// ============================================================================
// IRQ — Encoder + Encoder Button + External Button
// ============================================================================
void pot_irq(uint gpio, uint32_t events) {

    if (gpio == POT_CLK_PIN || gpio == POT_DT_PIN) {
        uint8_t clk = gpio_get(POT_CLK_PIN);
        uint8_t dt  = gpio_get(POT_DT_PIN);
        uint8_t current_state = (clk << 1) | dt;
        uint8_t combined = (last_state << 2) | current_state;

        if (combined == 0b0001 || combined == 0b0111 ||
            combined == 0b1110 || combined == 0b1000) {
            if (count < 10) count++;
            enc_moved = true;
        } else if (combined == 0b0010 || combined == 0b0100 ||
                   combined == 0b1101 || combined == 0b1011) {
            if (count > 0) count--;
            enc_moved = true;
        }

        last_state = current_state;
        bar_set(count);
    }

    if (gpio == POT_SW_PIN) {
        uint32_t now = time_us_32();
        if ((now - last_btn_time) < BTN_DEBOUNCE_US) return;
        last_btn_time = now;
        count = 5;
        btn_pressed = true;
        bar_set(count);
    }

    if (gpio == BUTTON_PIN) {
        uint32_t now = time_us_32();
        if ((now - last_ext_btn_time) < EXT_BTN_DEBOUNCE_US) return;
        last_ext_btn_time = now;
        ext_btn_pressed = true;
    }
}

// ============================================================================
int main() {
    stdio_init_all();

    // LED BAR
    for (int i = 0; i < 10; i++) {
        gpio_init(led_pins[i]);
        gpio_set_dir(led_pins[i], GPIO_OUT);
        gpio_put(led_pins[i], 0);
    }

    // SPI + MAX7219
    spi_init(SPI_PORT, 1 * 1000 * 1000);
    gpio_set_function(SPI_CLK_PIN,  GPIO_FUNC_SPI);
    gpio_set_function(SPI_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_init(SPI_CS_PIN);
    gpio_set_dir(SPI_CS_PIN, GPIO_OUT);
    gpio_put(SPI_CS_PIN, 1);
    max7219_init();
    matrix_display(pat_smile);

    // LCD
    lcd.init();
    lcd.cursor_off();
    lcd_refresh();

    // ENCODER
    gpio_init(POT_CLK_PIN); gpio_pull_up(POT_CLK_PIN);
    gpio_init(POT_DT_PIN);  gpio_pull_up(POT_DT_PIN);
    gpio_init(POT_SW_PIN);  gpio_pull_up(POT_SW_PIN);
    last_state = (gpio_get(POT_CLK_PIN) << 1) | gpio_get(POT_DT_PIN);

    gpio_set_irq_enabled_with_callback(POT_CLK_PIN,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &pot_irq);
    gpio_set_irq_enabled(POT_DT_PIN,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
    gpio_set_irq_enabled(POT_SW_PIN,
        GPIO_IRQ_EDGE_FALL, true);

    // External button
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);
    gpio_set_irq_enabled(BUTTON_PIN, GPIO_IRQ_EDGE_FALL, true);

    // Buzzer
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_put(BUZZER_PIN, 0);

    // LED_BUILTIN
    gpio_init(LED_BUILTIN_PIN);
    gpio_set_dir(LED_BUILTIN_PIN, GPIO_OUT);
    gpio_put(LED_BUILTIN_PIN, 1);

    // Startup sweep + beep
    for (int i = 0; i <= 10; i++) { bar_set(i); sleep_ms(60); }
    for (int i = 10; i >= 0; i--) { bar_set(i); sleep_ms(60); }
    bar_set(count);
    beep(1000);                      // startup confirmation beep

    printf("=== All Peripherals Ready ===\n");
    printf("L1:text | L2:text | M:smile | M:heart | M:checker | M:clear\n");
    printf("M:raw:XXXXXXXXXXXXXXXX | MB:0-15 | BZ:ms\n");

    while (true) {
        handle_serial();

        if (enc_moved) {
            printf("ENC → contrast %d/10\n", count);
            enc_moved = false;
        }
        if (btn_pressed) {
            printf("BTN (encoder) → contrast reset 5/10\n");
            btn_pressed = false;
        }
        if (ext_btn_pressed) {
            beep(1000);              // 100ms beep on button press
            printf("EXT BTN → beep!\n");
            ext_btn_pressed = false;
        }
        sleep_ms(10);
    }
}
