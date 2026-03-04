#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

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

// ===== KY-040 Rotary Encoder =====
#define POT_CLK_PIN 2
#define POT_DT_PIN  3
#define POT_SW_PIN  4

#define BTN_DEBOUNCE_US 200000  // 200ms button debounce

// ============================================================================

uint8_t led_pins[10] = {
    LED_0_PIN, LED_1_PIN, LED_2_PIN, LED_3_PIN, LED_4_PIN,
    LED_5_PIN, LED_6_PIN, LED_7_PIN, LED_8_PIN, LED_9_PIN
};

volatile int count = 0;
volatile bool enc_moved = false;
volatile bool btn_pressed = false;
volatile uint8_t last_state = 0;
volatile uint32_t last_btn_time = 0;

// ============================================================================

void bar_set(int level) {
    for (int i = 0; i < 10; i++) {
        gpio_put(led_pins[i], i < level);
    }
}

// ============================================================================

void pot_irq(uint gpio, uint32_t events) {

    // ===== ENCODER (Quadrature Decode) =====
    if (gpio == POT_CLK_PIN || gpio == POT_DT_PIN) {

        uint8_t clk = gpio_get(POT_CLK_PIN);
        uint8_t dt  = gpio_get(POT_DT_PIN);

        uint8_t current_state = (clk << 1) | dt;
        uint8_t combined = (last_state << 2) | current_state;

        // Clockwise transitions
        if (combined == 0b0001 ||
            combined == 0b0111 ||
            combined == 0b1110 ||
            combined == 0b1000) {

            if (count < 10) count++;
            enc_moved = true;
        }

        // Counter-clockwise transitions
        else if (combined == 0b0010 ||
                 combined == 0b0100 ||
                 combined == 0b1101 ||
                 combined == 0b1011) {

            if (count > 0) count--;
            enc_moved = true;
        }

        last_state = current_state;
        bar_set(count);
    }

    // ===== BUTTON =====
    if (gpio == POT_SW_PIN) {

        uint32_t now = time_us_32();
        if ((now - last_btn_time) < BTN_DEBOUNCE_US) return;
        last_btn_time = now;

        if (count < 10) count++;
        else count = 0;

        btn_pressed = true;
        bar_set(count);
    }
}

// ============================================================================

int main() {

    stdio_init_all();

    // LED BAR SETUP
    for (int i = 0; i < 10; i++) {
        gpio_init(led_pins[i]);
        gpio_set_dir(led_pins[i], GPIO_OUT);
        gpio_put(led_pins[i], 0);
    }

    // ENCODER SETUP
    gpio_init(POT_CLK_PIN); gpio_pull_up(POT_CLK_PIN);
    gpio_init(POT_DT_PIN);  gpio_pull_up(POT_DT_PIN);
    gpio_init(POT_SW_PIN);  gpio_pull_up(POT_SW_PIN);

    // Initialize starting encoder state
    last_state = (gpio_get(POT_CLK_PIN) << 1) | gpio_get(POT_DT_PIN);

    // Enable interrupts
    gpio_set_irq_enabled_with_callback(
        POT_CLK_PIN,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,
        true,
        &pot_irq
    );

    gpio_set_irq_enabled(
        POT_DT_PIN,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,
        true
    );

    gpio_set_irq_enabled(
        POT_SW_PIN,
        GPIO_IRQ_EDGE_FALL,
        true
    );

    // Startup sweep animation
    for (int i = 0; i <= 10; i++) { bar_set(i); sleep_ms(60); }
    for (int i = 10; i >= 0; i--) { bar_set(i); sleep_ms(60); }

    bar_set(count);

    printf("=== KY-040 + 2510SR-1 Stable Version ===\n");

    while (true) {

        if (enc_moved) {
            printf("ENC → %d/10\n", count);
            enc_moved = false;
        }

        if (btn_pressed) {
            printf("BTN → %d/10\n", count);
            btn_pressed = false;
        }

        sleep_ms(10);
    }
}
