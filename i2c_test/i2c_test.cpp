#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "lcd_display.hpp"

// ===== LCD =====
#define LCD_RS_PIN  7
#define LCD_E_PIN   8
#define LCD_D4_PIN  9
#define LCD_D5_PIN  10
#define LCD_D6_PIN  11
#define LCD_D7_PIN  12

// ===== I2C Slave =====
#define I2C_PORT       i2c0
#define I2C_SDA_PIN    0
#define I2C_SCL_PIN    1
#define I2C_SLAVE_ADDR 0x42
#define I2C_BUF_SIZE   40

LCDdisplay lcd(LCD_D4_PIN, LCD_D5_PIN, LCD_D6_PIN, LCD_D7_PIN,
               LCD_RS_PIN, LCD_E_PIN, 16, 2);

char row0[17] = "  AAV DISPLAY  ";
char row1[17] = " Waiting I2C.. ";

static char i2c_rx_buf[I2C_BUF_SIZE];
static int  i2c_rx_idx  = 0;
static bool i2c_msg_rdy = false;

// ============================================================================

void lcd_refresh() {
    lcd.clear();
    lcd.goto_pos(0, 0); lcd.print(row0);
    lcd.goto_pos(0, 1); lcd.print(row1);
}

void process_command(const char* buf) {
    printf("[RX] %s\n", buf);
    if (strncmp(buf, "L1:", 3) == 0) {
        // Check if combined packet: "L1:text|L2:text"
        const char* pipe = strchr(buf + 3, '|');
        if (pipe != NULL) {
            // Split into row0 and row1
            int len0 = pipe - (buf + 3);
            snprintf(row0, sizeof(row0), "%-16.*s", len0, buf + 3);
            snprintf(row1, sizeof(row1), "%-16s", pipe + 4); // skip "|L2:"
        } else {
            snprintf(row0, sizeof(row0), "%-16s", buf + 3);
        }
        lcd_refresh();

    } else if (strncmp(buf, "L2:", 3) == 0) {
        snprintf(row1, sizeof(row1), "%-16s", buf + 3);
        lcd_refresh();
    }
}


// ============================================================================
// I2C Slave IRQ — using raw hardware registers correctly
// ============================================================================
static void i2c_slave_irq_handler() {
    i2c_hw_t *hw = i2c_get_hw(I2C_PORT);
    uint32_t intr_stat = hw->intr_stat;

    if (intr_stat & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {
        uint8_t byte = (uint8_t)(hw->data_cmd & 0xFF);

        if (byte == '\n' || byte == '\r' || byte == '\0') {
            if (i2c_rx_idx > 0) {
                i2c_rx_buf[i2c_rx_idx] = '\0';
                i2c_rx_idx  = 0;
                i2c_msg_rdy = true;
            }
        } else {
            if (i2c_rx_idx < I2C_BUF_SIZE - 1)
                i2c_rx_buf[i2c_rx_idx++] = (char)byte;
        }
    }

    // Clear STOP_DET to re-arm for next transaction
    if (intr_stat & I2C_IC_INTR_STAT_R_STOP_DET_BITS) {
        (void)hw->clr_stop_det;
        // Flush any remaining partial buffer on STOP
        if (i2c_rx_idx > 0) {
            i2c_rx_buf[i2c_rx_idx] = '\0';
            i2c_rx_idx  = 0;
            i2c_msg_rdy = true;
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
    // Enable RX_FULL and STOP_DET interrupts only
    hw->intr_mask = I2C_IC_INTR_MASK_M_RX_FULL_BITS |
                    I2C_IC_INTR_MASK_M_STOP_DET_BITS;

    irq_set_exclusive_handler(I2C0_IRQ, i2c_slave_irq_handler);
    irq_set_enabled(I2C0_IRQ, true);
}

// ============================================================================
int main() {
    stdio_init_all();
    sleep_ms(2000);  // give USB serial time to connect

    lcd.init();
    lcd.cursor_off();
    lcd_refresh();

    i2c_slave_init();

    printf("[AAV] Display ready — I2C slave @ 0x%02X (GP0=SDA, GP1=SCL)\n", I2C_SLAVE_ADDR);

    while (true) {
        if (i2c_msg_rdy) {
            i2c_msg_rdy = false;
            process_command(i2c_rx_buf);
        }
        sleep_ms(5);
    }
}
