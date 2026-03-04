#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// ===== SPI CONFIG =====
#define SPI_PORT spi1
#define SPI_CLK_PIN 14
#define SPI_MOSI_PIN 15
#define SPI_CS_PIN 13

// =============================
// MAX7219 Register Addresses
// =============================
#define REG_NOOP        0x00
#define REG_DIGIT0      0x01
#define REG_DECODE_MODE 0x09
#define REG_INTENSITY   0x0A
#define REG_SCAN_LIMIT  0x0B
#define REG_SHUTDOWN    0x0C
#define REG_DISPLAYTEST 0x0F

// =============================
// SPI Send Function
// =============================
void max7219_write(uint8_t reg, uint8_t data) {
    uint8_t buffer[2] = {reg, data};

    gpio_put(SPI_CS_PIN, 0);
    spi_write_blocking(SPI_PORT, buffer, 2);
    gpio_put(SPI_CS_PIN, 1);
}

// =============================
// MAX7219 Init
// =============================
void max7219_init() {

    max7219_write(REG_SHUTDOWN, 0x01);      // Normal operation
    max7219_write(REG_DISPLAYTEST, 0x00);   // No display test
    max7219_write(REG_SCAN_LIMIT, 0x07);    // Scan all 8 rows
    max7219_write(REG_DECODE_MODE, 0x00);   // No BCD decode
    max7219_write(REG_INTENSITY, 0x08);     // Brightness (0–15)

    // Clear display
    for (int i = 1; i <= 8; i++)
        max7219_write(i, 0x00);
}

// =============================
// Clear Matrix
// =============================
void matrix_clear() {
    for (int i = 1; i <= 8; i++)
        max7219_write(i, 0x00);
}

// =============================
// Display Pattern
// =============================
void matrix_display(uint8_t pattern[8]) {
    for (int row = 0; row < 8; row++)
        max7219_write(row + 1, pattern[row]);
}

// =============================
// MAIN
// =============================
int main() {

    stdio_init_all();

    // SPI init
    spi_init(SPI_PORT, 1 * 1000 * 1000); // 1 MHz

    gpio_set_function(SPI_CLK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MOSI_PIN, GPIO_FUNC_SPI);

    gpio_init(SPI_CS_PIN);
    gpio_set_dir(SPI_CS_PIN, GPIO_OUT);
    gpio_put(SPI_CS_PIN, 1);

    max7219_init();

    // Smiley Face
    uint8_t smile[8] = {
        0b00111100,
        0b01000010,
        0b10100101,
        0b10000001,
        0b10100101,
        0b10011001,
        0b01000010,
        0b00111100
    };

    // Checker Pattern
    uint8_t checker[8] = {
        0b10101010,
        0b01010101,
        0b10101010,
        0b01010101,
        0b10101010,
        0b01010101,
        0b10101010,
        0b01010101
    };

    while (true) {

        matrix_display(smile);
        sleep_ms(2000);

        matrix_display(checker);
        sleep_ms(2000);

        matrix_clear();
        sleep_ms(500);
    }
}
