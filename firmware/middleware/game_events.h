/**
 * @file game_events.h
 * @brief Gerenciamento de eventos e feedbacks do jogo.
 *
 * Centraliza a lógica de disparo de notificações visuais e sonoras
 * em resposta a eventos ocorridos durante a execução.
 *
 * @author
 * @date 2026-03-01
 */

#ifndef GAME_EVENTS_H
#define GAME_EVENTS_H

/**
 * @brief Dispara o feedback visual e sonoro para marcos de pontuação.
 *
 * Ativa o buzzer e solicita um efeito especial nos LEDs quando um
 * novo marco (ex: múltiplos de 10) é atingido.
 */
void trigger_milestone_feedback(void);

#endif /* GAME_EVENTS_H */
