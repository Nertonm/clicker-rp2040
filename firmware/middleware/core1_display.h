/**
 * @file core1_display.h
 * @brief Interface para o ponto de entrada do Core 1 (Display dedicado).
 */

#ifndef CORE1_DISPLAY_H
#define CORE1_DISPLAY_H

/**
 * @brief Função de entrada para o Core 1.
 *
 * Esta função deve ser passada para multicore_launch_core1(). Ela assume
 * o controle exclusivo da matriz de LEDs e atualiza o display SSD1306.
 */
void core1_display_entry(void);

#endif /* CORE1_DISPLAY_H */
