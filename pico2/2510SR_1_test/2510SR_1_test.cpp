#include <stdio.h>
#include "pico/stdlib.h"

// ====== 2510SR-1 LED BAR PINS ======
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

// LED array (using your exact order)
uint8_t led_pins[10] = {
    LED_0_PIN, LED_1_PIN, LED_2_PIN, LED_3_PIN, LED_4_PIN,
    LED_5_PIN, LED_6_PIN, LED_7_PIN, LED_8_PIN, LED_9_PIN
};

// =============================
// Clear all LEDs
// =============================
void clear_leds() {
    for (int i = 0; i < 10; i++)
        gpio_put(led_pins[i], 0);
}

// =============================
// Knight Rider Pattern
// =============================
void pattern_running(uint delay_ms) {

    for (int i = 0; i < 10; i++) {
        clear_leds();
        gpio_put(led_pins[i], 1);
        sleep_ms(delay_ms);
    }

    for (int i = 8; i > 0; i--) {
        clear_leds();
        gpio_put(led_pins[i], 1);
        sleep_ms(delay_ms);
    }
}

// =============================
// Fill Bar Pattern
// =============================
void pattern_fill(uint delay_ms) {

    for (int i = 0; i < 10; i++) {
        gpio_put(led_pins[i], 1);
        sleep_ms(delay_ms);
    }

    for (int i = 0; i < 10; i++) {
        gpio_put(led_pins[i], 0);
        sleep_ms(delay_ms);
    }
}

// =============================
// Center-Out Burst
// =============================
void pattern_center(uint delay_ms) {

    for (int i = 0; i < 5; i++) {
        gpio_put(led_pins[4 - i], 1);
        gpio_put(led_pins[5 + i], 1);
        sleep_ms(delay_ms);
    }

    sleep_ms(200);
    clear_leds();
}

// =============================
// MAIN
// =============================
int main() {

    stdio_init_all();

    // Initialize all LED pins
    for (int i = 0; i < 10; i++) {
        gpio_init(led_pins[i]);
        gpio_set_dir(led_pins[i], GPIO_OUT);
        gpio_put(led_pins[i], 0);
    }

    while (true) {

        pattern_running(70);
        pattern_fill(50);
        pattern_center(80);
    }
}
