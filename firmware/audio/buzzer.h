#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

/**
 * @brief Inicializa o pino do buzzer para operação PWM.
 */
void buzzer_init(void);

/**
 * @brief Emite um tom com frequência e duração especificadas (não bloqueante).
 *
 * @param freq_hz Frequência do tom em Hz.
 * @param duration_ms Duração do tom em milissegundos.
 */
void buzzer_tone(uint32_t freq_hz, uint32_t duration_ms);

#endif // BUZZER_H
