#include "display.h"
#include "ssd1306_i2c.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include <string.h>

#define DISPLAY_SDA  14
#define DISPLAY_SCL  15

// Buffer estático — mesmo padrão do BitDogLabSmartDevicesControl
static uint8_t ssd_buf[ssd1306_buffer_length];
static struct render_area frame_area;

void display_init(void) {
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(DISPLAY_SDA, GPIO_FUNC_I2C);
    gpio_set_function(DISPLAY_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(DISPLAY_SDA);
    gpio_pull_up(DISPLAY_SCL);

    sleep_ms(10);
    ssd1306_init();

    frame_area.start_column = 0;
    frame_area.end_column   = ssd1306_width - 1;
    frame_area.start_page   = 0;
    frame_area.end_page     = ssd1306_n_pages - 1;
    calculate_render_area_buffer_length(&frame_area);

    display_clear();
    display_show();
}

void display_clear(void) {
    memset(ssd_buf, 0, ssd1306_buffer_length);
}

void display_text(uint8_t linha, uint8_t col, const char *texto) {
    // linha = índice de texto (0–7), col = pixel x (0–120)
    int16_t y = linha * 8;
    ssd1306_draw_string(ssd_buf, col, y, (char *)texto);
}

void display_show(void) {
    render_on_display(ssd_buf, &frame_area);
}
