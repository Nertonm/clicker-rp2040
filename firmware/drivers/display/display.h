#pragma once
#include <stdint.h>

void display_init(void);
void display_clear(void);
void display_text(uint8_t linha, uint8_t col, const char *texto);
void display_show(void);
