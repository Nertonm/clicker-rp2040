/**
 * @file cookie_bitmap.h
 * @brief Definições de bitmaps gráficos para o jogo.
 */

#pragma once
#include <stdint.h>

/** @brief Largura do bitmap do cookie em pixels. */
#define COOKIE_W 16
/** @brief Altura do bitmap do cookie em pixels. */
#define COOKIE_H 16

/**
 * @brief Bitmap 16x16 representando um cookie.
 *
 * Cada entrada de 16 bits representa uma linha horizontal do desenho.
 * Bit 1 indica pixel aceso, Bit 0 indica pixel apagado.
 */
static const uint16_t cookie_bitmap[COOKIE_H] = {
    0b0000011111100000, 0b0000111111110000, 0b0001111111111000,
    0b0011101111011100, 0b0011111111111100, 0b0111111011111110,
    0b0111110111111110, 0b0111111111011110, 0b0111101111111110,
    0b0111111111101110, 0b0011111111111100, 0b0011101111011100,
    0b0001111111111000, 0b0000111111110000, 0b0000011111100000,
    0b0000000000000000,
};
