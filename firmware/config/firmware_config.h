/**
 * @file firmware_config.h
 * @brief Parâmetros de configuração de comportamento do firmware.
 *
 * Contém definições de tempos de polling, tamanhos de fila e durações
 * de efeitos visuais/lógicos do jogo.
 *
 * @author
 * @date 2026-03-01
 */

#ifndef FIRMWARE_CONFIG_H
#define FIRMWARE_CONFIG_H

/** @name Configurações de Tarefas */
/** @{ */
#define CLICK_QUEUE_LEN 32        /**< Profundidade máxima da fila de cliques. */
#define DISPLAY_PERIOD_MS 150     /**< Intervalo de atualização da interface visual (ms). */
#define MONITOR_PERIOD_MS 5000    /**< Intervalo de log de telemetria (ms). */
#define RPC_POLL_PERIOD_MS 100    /**< Latência de resposta da tarefa RPC (ms). */
#define SCORES_REFRESH_MS 5000    /**< Intervalo de busca de scores globais (ms). */
#define RPC_REGISTER_RETRY_MS 5000 /**< Intervalo de sondagem de reconexão em modo OFFLINE (ms). */
/** @} */

/** @name Regras de Jogo */
/** @{ */
#define TURBO_DURATION_MS 10000   /**< Duração do efeito Power-up (ms). */
#define MILESTONE_GLOW_TICKS 10   /**< Duração do efeito visual de milestone (em ciclos de display). */
/** @} */

#endif /* FIRMWARE_CONFIG_H */
