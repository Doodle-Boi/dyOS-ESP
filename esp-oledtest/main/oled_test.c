#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "driver/i2c_master.h"

#define OLED_SDA_GPIO       21
#define OLED_SCL_GPIO       22
#define OLED_I2C_PORT       I2C_NUM_0
#define OLED_I2C_ADDRESS    0x3C
#define OLED_WIDTH          128
#define OLED_HEIGHT         64
#define OLED_PAGES          (OLED_HEIGHT / 8)

static const char *TAG = "oled_test";
static i2c_master_dev_handle_t oled_dev;
static uint8_t framebuffer[OLED_WIDTH * OLED_PAGES];

/* Tiny 5x7 font for the characters used by this test.
   Each byte is one vertical column; low 7 bits are the pixels. */
typedef struct {
    char ch;
    uint8_t col[5];
} glyph_t;

static const glyph_t font[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00}},
    {'A', {0x7E,0x11,0x11,0x11,0x7E}},
    {'D', {0x7F,0x41,0x41,0x22,0x1C}},
    {'E', {0x7F,0x49,0x49,0x49,0x41}},
    {'L', {0x7F,0x40,0x40,0x40,0x40}},
    {'O', {0x3E,0x41,0x41,0x41,0x3E}},
    {'S', {0x46,0x49,0x49,0x49,0x31}},
    {'T', {0x01,0x01,0x7F,0x01,0x01}},
    {'1', {0x00,0x42,0x7F,0x40,0x00}},
    {'2', {0x62,0x51,0x49,0x49,0x46}},
    {'4', {0x18,0x14,0x12,0x7F,0x10}},
    {'6', {0x3E,0x49,0x49,0x49,0x30}},
    {'8', {0x36,0x49,0x49,0x49,0x36}},
    {'x', {0x44,0x28,0x10,0x28,0x44}},
};

static esp_err_t oled_write_cmd(uint8_t cmd)
{
    uint8_t packet[2] = {0x00, cmd};
    return i2c_master_transmit(oled_dev, packet, sizeof(packet), 1000);
}

static esp_err_t oled_write_data(const uint8_t *data, size_t len)
{
    /* Send the I2C data-control byte plus framebuffer bytes in short chunks. */
    uint8_t packet[129];
    packet[0] = 0x40;
    while (len > 0) {
        size_t chunk = len > 128 ? 128 : len;
        memcpy(&packet[1], data, chunk);
        esp_err_t err = i2c_master_transmit(oled_dev, packet, chunk + 1, 1000);
        if (err != ESP_OK) return err;
        data += chunk;
        len -= chunk;
    }
    return ESP_OK;
}

static void oled_init(void)
{
    const uint8_t init_seq[] = {
        0xAE,       /* display off */
        0xD5, 0x80, /* clock */
        0xA8, 0x3F, /* multiplex: 64 rows */
        0xD3, 0x00, /* display offset */
        0x40,       /* start line */
        0x8D, 0x14, /* charge pump */
        0x20, 0x02, /* page addressing mode */
        0xA1,       /* segment remap */
        0xC8,       /* COM scan direction */
        0xDA, 0x12, /* COM pins for 128x64 */
        0x81, 0x8F, /* contrast */
        0xD9, 0xF1, /* pre-charge */
        0xDB, 0x40, /* VCOM detect */
        0xA4,       /* use RAM contents */
        0xA6,       /* normal display */
        0xAF        /* display on */
    };
    for (size_t i = 0; i < sizeof(init_seq); i++) {
        ESP_ERROR_CHECK(oled_write_cmd(init_seq[i]));
    }
}

static void set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    size_t index = (size_t)(y / 8) * OLED_WIDTH + x;
    uint8_t mask = (uint8_t)(1U << (y & 7));
    if (on) framebuffer[index] |= mask;
    else framebuffer[index] &= (uint8_t)~mask;
}

static void draw_char(int x, int y, char c)
{
    const uint8_t *cols = font[0].col;
    for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); i++) {
        if (font[i].ch == c) {
            cols = font[i].col;
            break;
        }
    }
    for (int cx = 0; cx < 5; cx++) {
        for (int cy = 0; cy < 7; cy++) {
            set_pixel(x + cx, y + cy, (cols[cx] & (1U << cy)) != 0);
        }
    }
}

static void draw_text(int x, int y, const char *s)
{
    while (*s) {
        draw_char(x, y, *s++);
        x += 6;
    }
}

static void draw_frame(int phase)
{
    memset(framebuffer, 0, sizeof(framebuffer));

    /* Outer border and a divider line. */
    for (int x = 0; x < OLED_WIDTH; x++) {
        set_pixel(x, 0, true);
        set_pixel(x, OLED_HEIGHT - 1, true);
        set_pixel(x, 27, true);
    }
    for (int y = 0; y < OLED_HEIGHT; y++) {
        set_pixel(0, y, true);
        set_pixel(OLED_WIDTH - 1, y, true);
    }

    draw_text(10, 8, "OLED TEST");
    draw_text(10, 17, "128 x 64");

    /* Animated checker bars to confirm pixels and refresh work. */
    for (int y = 35; y < 59; y++) {
        for (int x = 8; x < 120; x++) {
            bool on = (((x / 4) + (y / 4) + phase) & 1) == 0;
            set_pixel(x, y, on);
        }
    }
    /* Keep the border visible over the pattern. */
    for (int x = 0; x < OLED_WIDTH; x++) {
        set_pixel(x, 0, true);
        set_pixel(x, OLED_HEIGHT - 1, true);
    }
}

static esp_err_t oled_refresh(void)
{
    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        ESP_RETURN_ON_ERROR(oled_write_cmd((uint8_t)(0xB0 | page)), TAG, "set page");
        ESP_RETURN_ON_ERROR(oled_write_cmd(0x00), TAG, "set low column");
        ESP_RETURN_ON_ERROR(oled_write_cmd(0x10), TAG, "set high column");
        ESP_RETURN_ON_ERROR(oled_write_data(&framebuffer[page * OLED_WIDTH], OLED_WIDTH),
                            TAG, "write page");
    }
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting 128x64 I2C OLED test");
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .i2c_port = OLED_I2C_PORT,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDRESS,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_config, &oled_dev));

    esp_err_t probe = i2c_master_probe(bus, OLED_I2C_ADDRESS, 1000);
    if (probe != ESP_OK) {
        ESP_LOGE(TAG, "No OLED ACK at 0x%02X (error %s). Check wiring/address.",
                 OLED_I2C_ADDRESS, esp_err_to_name(probe));
        return;
    }

    ESP_LOGI(TAG, "OLED responded at 0x%02X", OLED_I2C_ADDRESS);
    oled_init();

    int phase = 0;
    while (1) {
        draw_frame(phase++);
        ESP_ERROR_CHECK(oled_refresh());
        vTaskDelay(pdMS_TO_TICKS(350));
    }
}
