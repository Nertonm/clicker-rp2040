#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdint.h> // para uint32_t, etc
#include <stdbool.h> // para bool, true, false


// Número máximo de nós suportados
#define MAX_NODES 3

// Centralizei aqui para editar facil dps
typedef enum {
    STATUS_CONNECTING,
    STATUS_ONLINE,
    STATUS_OFFLINE,
    STATUS_SYNCING
} connection_status_t;

// Inicia uma vez o boot, antes do core1
void shared_state_init(void);

// IRQ do botão A
void shared_state_increment_clicks(void);

// IRQ do botão B
// não sei se vai ter algo aqui, mas deixo o espaço pra não precisar mexer na estrutura depois

// Loop principal Core 0
uint32_t shared_state_take_clicks(void); // retorna o número de cliques acumulados e zera o contador
void shared_state_set_status(connection_status_t s);
void shared_state_set_scores(uint32_t global, uint32_t local,uint32_t *node_scores, uint8_t count);

// Loop principal Core 1
connection_status_t shared_state_get_status(void);
bool shared_state_get_debug_mode(void);
// Retorna os scores atuais: global, local e por nó (em ordem de ID)
void shared_state_get_scores(uint32_t *out_global, uint32_t *out_local, uint32_t *out_nodes, uint8_t count);

#endif // SHARED_STATE_H