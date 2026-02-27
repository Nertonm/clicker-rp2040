#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#ifndef ssd1306_inc_h
#define ssd1306_inc_h

#define ssd1306_height 64 // Define a altura do display (32 pixels)
#define ssd1306_width 128 // Define a largura do display (128 pixels)

#define ssd1306_i2c_address _u(0x3C) // Define o endereço do i2c do display

#define ssd1306_i2c_clock 400 // Define o tempo do clock (pode ser aumentado)

// Comandos de configuração (endereços)
#define ssd1306_set_memory_mode _u(0x20)
#define ssd1306_set_column_address _u(0x21)
#define ssd1306_set_page_address _u(0x22)
#define ssd1306_set_horizontal_scroll _u(0x26)
#define ssd1306_set_scroll _u(0x2E)

#define ssd1306_set_display_start_line _u(0x40)

#define ssd1306_set_contrast _u(0x81)
#define ssd1306_set_charge_pump _u(0x8D)

#define ssd1306_set_segment_remap _u(0xA0)
#define ssd1306_set_entire_on _u(0xA4)
#define ssd1306_set_all_on _u(0xA5)
#define ssd1306_set_normal_display _u(0xA6)
#define ssd1306_set_inverse_display _u(0xA7)
#define ssd1306_set_mux_ratio _u(0xA8)
#define ssd1306_set_display _u(0xAE)
#define ssd1306_set_common_output_direction _u(0xC0)
#define ssd1306_set_common_output_direction_flip _u(0xC0)

#define ssd1306_set_display_offset _u(0xD3)
#define ssd1306_set_display_clock_divide_ratio _u(0xD5)
#define ssd1306_set_precharge _u(0xD9)
#define ssd1306_set_common_pin_configuration _u(0xDA)
#define ssd1306_set_vcomh_deselect_level _u(0xDB)

#define ssd1306_page_height _u(8)
#define ssd1306_n_pages (ssd1306_height / ssd1306_page_height)
#define ssd1306_buffer_length (ssd1306_n_pages * ssd1306_width)

#define ssd1306_write_mode _u(0xFE)
#define ssd1306_read_mode _u(0xFF)

struct render_area {
    uint8_t start_column;
    uint8_t end_column;
    uint8_t start_page;
    uint8_t end_page;

    int buffer_length;
};

typedef struct {
  uint8_t width, height, pages, address;
  i2c_inst_t * i2c_port;
  bool external_vcc;
  uint8_t *ram_buffer;
  size_t bufsize;
  uint8_t port_buffer[2];
} ssd1306_t;

// Funções para texto (buffer estático)
void calculate_render_area_buffer_length(struct render_area *area);
void ssd1306_init(void);
void render_on_display(uint8_t *ssd, struct render_area *area);
void ssd1306_set_pixel(uint8_t *ssd, int x, int y, bool set);
void ssd1306_draw_line(uint8_t *ssd, int x_0, int y_0, int x_1, int y_1, bool set);
void ssd1306_draw_char(uint8_t *ssd, int16_t x, int16_t y, uint8_t character);
void ssd1306_draw_string(uint8_t *ssd, int16_t x, int16_t y, char *string);
void ssd1306_scroll(bool set);

// Funções para bitmap (struct ssd1306_t)
void ssd1306_init_bm(ssd1306_t *ssd, uint8_t width, uint8_t height, bool external_vcc, uint8_t address, i2c_inst_t *i2c);
void ssd1306_config(ssd1306_t *ssd);
void ssd1306_send_data(ssd1306_t *ssd);
void ssd1306_draw_bitmap(ssd1306_t *ssd, const uint8_t *bitmap);

#endif