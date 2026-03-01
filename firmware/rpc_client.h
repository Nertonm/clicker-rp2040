/**
 * @file rpc_client.h
 * @brief API de comunicação JSON-RPC com o servidor central.
 *
 * Provê a interface única de rede para o firmware, abstraindo a complexidade
 * de sockets e parsing de JSON para as tarefas de alto nível.
 *
 * @author
 * @date 2026-03-01
 */

#ifndef RPC_CLIENT_H
#define RPC_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Códigos de erro retornados pelas operações RPC.
 *
 * @note Campos não preenchidos nos resultados são inicializados com zero
 * (memset).
 */
typedef enum {
  RPC_OK = 0,            /**< Operação bem-sucedida. */
  RPC_TIMEOUT,           /**< Servidor não respondeu dentro do tempo limite. */
  RPC_DISCONNECTED,      /**< Socket fechado ou sem conexão física. */
  RPC_LAMPORT_VIOLATION, /**< Servidor rejeitou a requisição por timestamp
                            inválido. */
  RPC_RATE_EXCEEDED,     /**< Servidor limitou a taxa de cliques (anti-spam). */
  RPC_PARSE_ERROR        /**< Resposta JSON do servidor está mal formatada. */
} RpcError;

/**
 * @brief Resultado de operações simples (inicialização, registro).
 */
typedef struct {
  bool success; /**< Verdadeiro se a operação foi bem-sucedida. */
  RpcError
      error_code; /**< Código de erro detalhado (RPC_OK se success for true). */
} RpcSimpleResult;

/**
 * @brief Resultado detalhado de operações de clique e sincronização.
 *
 * Campos são válidos apenas se success for verdadeiro.
 */
typedef struct {
  bool success;        /**< Verdadeiro se a operação foi bem-sucedida. */
  RpcError error_code; /**< Código de erro detalhado. */

  int global_score;         /**< Pontuação global acumulada de todos os nós. */
  int local_score;          /**< Pontuação individual deste nó. */
  uint32_t lamport_ts;      /**< Timestamp Lamport atualizado pelo servidor. */
  bool milestone_triggered; /**< Indica se um novo marco de pontuação foi
                               atingido. */
  int milestone_value;      /**< Valor do marco atingido (válido se
                               milestone_triggered for true). */
  int accepted_clicks;      /**< Quantidade de cliques aceitos pelo servidor. */
} RpcClickResult;

/**
 * @brief Resultado da ativação de Power-up.
 */
typedef struct {
  bool success;        /**< Verdadeiro se a ativação foi bem-sucedida. */
  RpcError error_code; /**< Código de erro detalhado. */

  int powerup_remaining_s; /**< Segundos restantes de duração do power-up ativo.
                            */
} RpcPowerupResult;

/**
 * @brief Resultado de consulta de placar (scores).
 */
typedef struct {
  bool success;        /**< Verdadeiro se a consulta foi bem-sucedida. */
  RpcError error_code; /**< Código de erro detalhado. */

  int global_score;   /**< Pontuação global total. */
  int node_scores[3]; /**< Pontuações individuais por ID de nó [0, 1, 2]. */
} RpcScoreResult;

/**
 * @brief Inicializa o cliente RPC e as estruturas de rede.
 *
 * Deve ser chamado uma única vez durante o boot do sistema.
 *
 * @return RpcSimpleResult Status da inicialização.
 */
RpcSimpleResult rpc_init(void);

/**
 * @brief Registra o identificador deste nó no servidor central.
 *
 * @param[in] node_id Identificador único do nó (0, 1 ou 2).
 * @return RpcSimpleResult Status do registro.
 */
RpcSimpleResult rpc_register_node(uint8_t node_id);

/**
 * @brief Envia uma quantidade de cliques para o servidor.
 *
 * @param[in] clicks Quantidade de cliques a serem enviados.
 * @param[in] lamport_ts Valor atual do relógio de Lamport local.
 * @return RpcClickResult Resultado da operação e scores atualizados.
 */
RpcClickResult rpc_add_clicks(int clicks, uint32_t lamport_ts);

/**
 * @brief Solicita a ativação do Power-up (multiplicador de pontos) no servidor.
 *
 * @return RpcPowerupResult Status da ativação e tempo de duração.
 */
RpcPowerupResult rpc_activate_powerup(void);

/**
 * @brief Obtém as pontuações atuais de todos os nós da rede.
 *
 * @return RpcScoreResult Estrutura contendo o placar atualizado.
 */
RpcScoreResult rpc_get_scores(void);

/**
 * @brief Sincroniza cliques que foram acumulados enquanto o dispositivo estava
 * offline.
 *
 * @param[in] accumulated_clicks Total de cliques acumulados.
 * @param[in] lamport_ts Último timestamp de Lamport conhecido.
 * @return RpcClickResult Resultado da sincronização.
 */
RpcClickResult rpc_sync_offline(int accumulated_clicks, uint32_t lamport_ts);

/**
 * @brief Define manualmente o endereço IP e porta do servidor RPC.
 *
 * @param[in] ip_str String contendo o endereço IPv4 (ex: "192.168.1.5").
 * @param[in] port Porta TCP do serviço RPC.
 */
void rpc_client_set_server(const char *ip_str, uint16_t port);

/**
 * @brief Configura o cliente para utilizar o endereço IP de fallback
 * pré-definido.
 *
 * Utilizado quando a descoberta automática de serviço falha.
 */
void rpc_client_set_server_fallback(void);

/**
 * @brief Verifica se existe uma conexão ativa com o servidor RPC.
 *
 * @return bool Verdadeiro se estiver conectado.
 */
bool rpc_is_connected(void);

#endif // RPC_CLIENT_H
