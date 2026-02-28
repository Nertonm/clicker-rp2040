#ifndef WS2812_H
#define WS2812_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Inicializa a matriz de LEDs WS2812 via PIO.
 * Usa as definições de WS2812_PIN e WS2812_NUM_LEDS de hardware_config.h.
 */
void led_matrix_init(void);

/**
 * @brief Define a cor de um LED específico na matriz.
 *
 * @param index Índice do LED (0 a WS2812_NUM_LEDS - 1)
 * @param r Componente Vermelho (0-255)
 * @param g Componente Verde (0-255)
 * @param b Componente Azul (0-255)
 */
void led_set(int index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Apaga todos os LEDs da matriz.
 */
void led_clear_all(void);

#endif // WS2812_H
