/**
 * @file shared_state.h
 * @brief Gerenciamento de estado compartilhado entre núcleos (Core 0 e Core 1).
 *
 * Provê uma interface thread-safe para acesso a dados globais como pontuação,
 * status de conexão e flags de eventos, utilizando spinlocks do RP2040.
 *
 * @author
 * @date 2026-03-01
 */

#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "middleware/rpc_types.h"

/**
 * @brief Número máximo de nós suportados na rede.
 */
#define MAX_NODES 3

/**
 * @brief Estados possíveis da conexão do firmware com o servidor.
 */
typedef enum {
  STATUS_CONNECTING, /**< Tentando estabelecer conexão inicial. */
  STATUS_ONLINE,     /**< Conectado e sincronizado com o servidor. */
  STATUS_OFFLINE,    /**< Sem conexão ou erro de rede. */
  STATUS_SYNCING     /**< Em processo de sincronização de dados pendentes. */
} connection_status_t;

/**
 * @brief Inicializa o estado compartilhado e o spinlock de proteção.
 *
 * @note Deve ser chamado pelo Core 0 antes de iniciar o Core 1 ou qualquer tarefa FreeRTOS.
 */
void shared_state_init(void);

/* --- Funções de Acesso (Thread-safe via Spinlock) --- */

/**
 * @brief Obtém a quantidade de cliques pendentes de envio.
 * @return uint32_t Número de cliques acumulados localmente.
 */
uint32_t shared_state_get_pending_clicks(void);

/**
 * @brief Incrementa atomicamente o contador de cliques pendentes.
 * Chamado normalmente em resposta a interrupções de hardware (botões).
 */
void shared_state_increment_pending_clicks(void);

/**
 * @brief Lê e zera atomicamente o contador de cliques pendentes.
 *
 * @return uint32_t Número de cliques consumidos para processamento.
 */
uint32_t shared_state_take_pending_clicks(void);

/**
 * @brief Restaura uma quantidade de cliques ao contador pendente.
 *
 * Utilizado para restaurar cliques em caso de falha de envio para o servidor.
 * @param[in] n Quantidade de cliques a serem restaurados.
 */
void shared_state_restore_clicks(uint32_t n);

/**
 * @brief Obtém a pontuação (score) local do dispositivo.
 * @return uint32_t Pontuação local.
 */
uint32_t shared_state_get_local_score(void);

/**
 * @brief Define a pontuação local.
 * @param[in] score Novo valor da pontuação.
 * @note Apenas Core 0 deve realizar a escrita.
 */
void shared_state_set_local_score(uint32_t score);

/**
 * @brief Obtém a pontuação global acumulada (todos os nós).
 * @return uint32_t Pontuação global.
 */
uint32_t shared_state_get_global_score(void);

/**
 * @brief Define a pontuação global.
 * @param[in] score Novo valor da pontuação global.
 * @note Apenas Core 0 deve realizar a escrita.
 */
void shared_state_set_global_score(uint32_t score);

/**
 * @brief Obtém as pontuações individuais de todos os nós conhecidos.
 *
 * @param[out] out_scores Ponteiro para array que receberá os scores.
 * @param[in] count Quantidade máxima de elementos a ler.
 */
void shared_state_get_node_scores(uint32_t *out_scores, uint8_t count);

/**
 * @brief Atualiza os scores de múltiplos nós simultaneamente.
 *
 * @param[in] in_scores Ponteiro para array com os novos scores.
 * @param[in] count Quantidade de elementos no array.
 * @note Apenas Core 0 deve realizar a escrita.
 */
void shared_state_set_node_scores(const uint32_t *in_scores, uint8_t count);

/**
 * @brief Atualiza atomicamente todos os campos de estado derivados de um
 *        resultado de clique bem-sucedido.
 *
 * Consolida a atualização de local_score, global_score e milestone_triggered
 * sob um único acquire/release do spinlock, eliminando janelas de
 * inconsistência entre atualizações individuais.
 *
 * @param[in] result Ponteiro para o resultado RPC. Deve ser não-nulo e válido
 *                   (result->success == true garantido pelo chamador).
 */
void shared_state_set_scores(const RpcClickResult *result);

/**
 * @brief Obtém o status atual da conexão.
 * @return connection_status_t Estado atual (Online, Offline, etc).
 */
connection_status_t shared_state_get_connection_status(void);

/**
 * @brief Define o status da conexão.
 * @param[in] status Novo estado de conexão.
 */
void shared_state_set_connection_status(connection_status_t status);

/**
 * @brief Adquire manualmente o spinlock do shared_state.
 *
 * Utilizado por módulos externos (ex: lamport.c) que necessitam compartilhar a mesma trava de exclusão mútua.
 * @param[out] save Ponteiro para armazenar o estado das interrupções (IRQ) antes da trava.
 */
void shared_state_lock_enter(uint32_t *save);

/**
 * @brief Libera manualmente o spinlock do shared_state.
 * @param[in] save Valor retornado por shared_state_lock_enter para restaurar o estado de IRQ.
 */
void shared_state_lock_exit(uint32_t save);

/**
 * @brief Verifica se um marco de pontuação (milestone) foi atingido.
 * @return bool Verdadeiro se atingido.
 */
bool shared_state_get_milestone_triggered(void);

/**
 * @brief Define se um marco de pontuação foi atingido.
 * @param[in] triggered Estado do marco.
 */
void shared_state_set_milestone_triggered(bool triggered);

/**
 * @brief Lê e limpa atomicamente a flag de milestone.
 * @return bool Estado da flag antes de ser limpa.
 */
bool shared_state_take_milestone_triggered(void);

/**
 * @brief Verifica se houve pedido de flash do LED.
 * @return bool Verdadeiro se solicitado.
 */
bool shared_state_get_led_flash_requested(void);

/**
 * @brief Solicita um flash no LED.
 * @param[in] requested Verdadeiro para solicitar.
 */
void shared_state_set_led_flash_requested(bool requested);

/**
 * @brief Lê e limpa o pedido de flash do LED.
 * @return bool Verdadeiro se havia um pedido pendente.
 */
bool shared_state_take_led_flash_requested(void);

/**
 * @brief Verifica se o firmware está usando o IP de fallback.
 * @return bool Verdadeiro se em modo fallback.
 */
bool shared_state_get_fallback_in_use(void);

/**
 * @brief Define o uso do IP de fallback.
 * @param[in] in_use Estado do fallback.
 */
void shared_state_set_fallback_in_use(bool in_use);

/**
 * @brief Verifica se há erro ativo no servidor.
 * @return bool Verdadeiro se houver erro ativo.
 */
bool shared_state_get_server_error_active(void);

/**
 * @brief Define estado de erro no servidor.
 * @param[in] active Verdadeiro para indicar erro.
 */
void shared_state_set_server_error_active(bool active);

/**
 * @brief Solicita ativação do modo Turbo/Power-up.
 */
void shared_state_request_turbo_activation(void);

/**
 * @brief Lê e limpa o pedido de ativação de turbo.
 * @return bool Verdadeiro se solicitado.
 */
bool shared_state_take_turbo_activation_requested(void);

/**
 * @brief Verifica se o modo Turbo está ativo no momento.
 * @return bool Verdadeiro se ativo.
 */
bool shared_state_get_turbo_active(void);

/**
 * @brief Define se o modo Turbo está ativo.
 * @param[in] active Estado do turbo.
 */
void shared_state_set_turbo_active(bool active);

/**
 * @brief Obtém o tempo de expiração do modo Turbo (em ms).
 * @return uint32_t Timestamp (uptime_ms) de expiração.
 */
uint32_t shared_state_get_turbo_until_ms(void);

/**
 * @brief Define o tempo de expiração do modo Turbo.
 * @param[in] until_ms Timestamp (uptime_ms) de expiração.
 */
void shared_state_set_turbo_until_ms(uint32_t until_ms);

#endif // SHARED_STATE_H