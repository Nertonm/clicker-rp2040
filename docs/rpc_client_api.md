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
- `RPC_OFFLINE_QUEUED`: Sem conexão, cliques enfileirados (não é erro crítico)
- `RPC_LAMPORT_VIOLATION`: Servidor rejeitou timestamp (atualizar Lamport)
- `RPC_RATE_EXCEEDED`: Servidor limitou taxa (alguns cliques rejeitados)
- `RPC_DISCONNECTED`: Socket fechado
- `RPC_TIMEOUT`: Servidor não respondeu
- `RPC_PARSE_ERROR`: Resposta JSON inválida

## Reconexão Automática
- A reconexão é dirigida pela `task_rpc` via chamadas periódicas a `rpc_poll()`.
- O intervalo de tentativa é limitado internamente (~2s).
- A fila offline é drenada quando a conexão volta.

## Exemplos
Ver `firmware/main.c` para uso completo.

## Comportamento Atual

- `rpc_init()` inicializa o estado interno e configura fallback; a conexão é lazy.
- `rpc_register_node(node_id)` estabelece conexão e registra o nó.
- `rpc_add_clicks(clicks, lamport_ts)` envia online quando possível; offline retorna `RPC_OFFLINE_QUEUED`.
- `rpc_activate_powerup()` ativa turbo (botão B) no servidor quando online.
- `rpc_get_scores()` atualiza placar autoritativo periodicamente.
- `rpc_client_set_server(...)` e `rpc_client_set_server_fallback()` definem o endpoint alvo.

## Melhorias Futuras

- [ ] Trocar parser sscanf por cJSON para robustez
- [ ] Adicionar métricas de latência e taxa de reconexão
- [ ] Testes unitários com servidor mock
