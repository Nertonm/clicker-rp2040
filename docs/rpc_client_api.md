# API rpc_client.h

## Visão Geral
Interface pública para comunicação JSON-RPC com servidor.
Encapsula completamente lwIP, sockets e parsing JSON.

## Inicialização
1. Chamar `rpc_init()` no boot
2. Chamar `rpc_register_node(NODE_ID)`
3. Loop principal pode usar `rpc_add_clicks()` livremente

## Tratamento de Erros
Todas as funções retornam structs com `success` e `error_code`.
Sempre checar `success` antes de acessar outros campos.

### Códigos de Erro
- `RPC_OK`: Sucesso
- `RPC_LAMPORT_VIOLATION`: Servidor rejeitou timestamp (atualizar Lamport)
- `RPC_RATE_EXCEEDED`: Servidor limitou taxa (alguns cliques rejeitados)
- `RPC_DISCONNECTED`: Socket fechado ou erro de conexão
- `RPC_TIMEOUT`: Servidor não respondeu (timeout no recv)
- `RPC_PARSE_ERROR`: Resposta JSON inválida

## Comportamento de Retry
- A reconexão e tentativa de envio acontecem dentro de cada chamada (`rpc_add_clicks`, etc).
- Há 3 tentativas de envio por operação usando `vTaskDelay()` (não `sleep_ms()` por causa do FreeRTOS) com backoff de {100, 200, 400} ms.
- Worst-case latency por chamada: `2s (timeout) * 3 tentativas + 100ms + 200ms = 6,3s`.
- Uma primitiva interna `rpc_call_once()` tenta `connect -> send -> recv` (nova conexão por tentativa) e ela não é exposta na API.
- Se todas as falharam e o resultado for offline/rate limit, a task de rede em `firmware/tasks/task_rpc.c` é responsável por usar `shared_state_restore_clicks()` para o retry natural via loop principal.

## Exemplos
Ver `firmware/tasks/task_rpc.c` para uso completo.

## Comportamento Atual

- `rpc_init()` inicializa o estado interno e configura fallback; a conexão é lazy.
- `rpc_register_node(node_id)` estabelece conexão e registra o nó.
- `rpc_add_clicks(clicks, lamport_ts)` envia online quando possível; falha retorna erro para tratamento na task.
- `rpc_activate_powerup()` ativa turbo (botão B) no servidor quando online.
- `rpc_get_scores()` atualiza placar autoritativo periodicamente.
- `rpc_client_set_server(...)` e `rpc_client_set_server_fallback()` definem o endpoint alvo.

## Melhorias Futuras

- [ ] Trocar parser sscanf por cJSON para robustez
- [ ] Adicionar métricas de latência e taxa de reconexão
- [ ] Testes unitários com servidor mock
- [ ] Implementar socket non-blocking para `connect()` (atualmente o connect bloqueia, adicionaria complexidade de `select()`)
