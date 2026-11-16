
#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
// ---------------- PIN CONFIGURATION ----------------

#define ROW_COUNT 4
#define COL_COUNT 4

static const gpio_num_t row_pins[ROW_COUNT] = {
    GPIO_NUM_16,
    GPIO_NUM_17,
    GPIO_NUM_18,
    GPIO_NUM_19
};

static const gpio_num_t col_pins[COL_COUNT] = {
    GPIO_NUM_25,
    GPIO_NUM_26,
    GPIO_NUM_27,
    GPIO_NUM_32
};

// Key layout
static const char *keys[ROW_COUNT][COL_COUNT] = {
    {"1", "2", "3", "A"},
    {"4", "5", "6", "B"},
    {"7", "8", "9", "C"},
    {"*", "0", "#", "D"}
};

// ---------------- DEBOUNCING ----------------

#define DEBOUNCE_LIMIT 3

static bool key_state[ROW_COUNT][COL_COUNT] = {0};
static bool candidate_state[ROW_COUNT][COL_COUNT] = {0};
static int debounce_count[ROW_COUNT][COL_COUNT] = {0};

// ---------------- GPIO INITIALIZATION ----------------

static void keyboard_init(void)
{
    uint64_t row_mask = 0;
    uint64_t col_mask = 0;

    for (int i = 0; i < ROW_COUNT; i++) {
        row_mask |= (1ULL << row_pins[i]);
    }

    for (int i = 0; i < COL_COUNT; i++) {
        col_mask |= (1ULL << col_pins[i]);
    }

    // Configure rows as outputs
    gpio_config_t row_config = {
        .pin_bit_mask = row_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&row_config));

    // Keep all rows inactive (HIGH)
    for (int i = 0; i < ROW_COUNT; i++) {
        ESP_ERROR_CHECK(gpio_set_level(row_pins[i], 1));
    }

    // Configure columns as inputs with pull-ups
    gpio_config_t col_config = {
        .pin_bit_mask = col_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&col_config));

    printf("\nKeyboard GPIO initialized.\n");
}

// ---------------- KEYBOARD SCANNER ----------------

static void keyboard_scan(void)
{
    for (int row = 0; row < ROW_COUNT; row++) {

        // Activate current row
        gpio_set_level(row_pins[row], 0);

        // Allow signal to settle
        esp_rom_delay_us(50);

        for (int col = 0; col < COL_COUNT; col++) {

            // LOW means pressed
            bool pressed = (gpio_get_level(col_pins[col]) == 0);

            // Debounce candidate changed
            if (pressed != candidate_state[row][col]) {
                candidate_state[row][col] = pressed;
                debounce_count[row][col] = 0;
            } else if (debounce_count[row][col] < DEBOUNCE_LIMIT) {
                debounce_count[row][col]++;
            }

            // Accept stable state
            if (debounce_count[row][col] == DEBOUNCE_LIMIT &&
                key_state[row][col] != candidate_state[row][col]) {

                key_state[row][col] = candidate_state[row][col];

                if (key_state[row][col]) {
                    printf(
                        "[PRESSED]  Key: %s | Row: %d | Column: %d\n",
                        keys[row][col],
                        row + 1,
                        col + 1
                    );
                } else {
                    printf(
                        "[RELEASED] Key: %s | Row: %d | Column: %d\n",
                        keys[row][col],
                        row + 1,
                        col + 1
                    );
                }
            }
        }

        // Deactivate current row
        gpio_set_level(row_pins[row], 1);
    }
}

// ---------------- MAIN ----------------

void app_main(void)
{
    printf("\n");
    printf("================================\n");
    printf("       dyOS KEYBOARD TEST       \n");
    printf("          4 x 4 MATRIX          \n");
    printf("================================\n");

    keyboard_init();

    printf("Ready! Press any key.\n\n");

    while (1) {
        keyboard_scan();

        // 10 ms scan interval
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

