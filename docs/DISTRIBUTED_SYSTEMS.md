# DISTRIBUTED_SYSTEMS.md - Características de Sistemas Distribuídos

Cada seção cobre uma característica de sistemas distribuídos: o conceito, como está implementado no código, os arquivos relevantes e como demonstrar na apresentação.

---

## 1. RPC como Middleware

### Requisito conceitual
Em um sistema com RPC, o cliente invoca operações remotas usando a mesma sintaxe de chamada de função local. A camada de middleware oculta completamente a rede.

### Implementação no projeto

O firmware não manipula sockets diretamente. Todo acesso à rede passa por `firmware/rpc_client.c`, que expõe funções C com tipos de retorno específicos por operação:

```c
RpcSimpleResult rpc_register_node(uint8_t node_id);
RpcClickResult  rpc_add_clicks(int clicks, uint32_t lamport_ts);
RpcClickResult  rpc_sync_offline(int accumulated_clicks, uint32_t lamport_ts);
RpcPowerupResult rpc_activate_powerup(void);
RpcScoreResult  rpc_get_scores(void);
RpcSimpleResult rpc_init(void);
```

Nenhum arquivo além de `rpc_client.c` conhece `lwip/sockets.h`.

### Schema JSON-RPC 2.0 utilizado

Requisicao (enviada pelo firmware, terminada com `\n`):

```json
{"jsonrpc":"2.0","method":"add_clicks","params":{"node_id":0,"clicks":5,"lamport_ts":42},"id":1}
```

Resposta de sucesso (enviada pelo servidor, terminada com `\n`):

```json
{"jsonrpc":"2.0","result":{"status":"SUCCESS","accepted_clicks":5,"global_score":105,"node_score":55,"lamport_ts":43,"milestone":false,"milestone_value":null},"id":1}
```

Resposta de erro:

```json
{"jsonrpc":"2.0","error":{"code":-32601,"message":"Method not found"},"id":1}
```

Campos obrigatorios: `jsonrpc` (sempre `"2.0"`), `method`, `params` (objeto ou lista), `id`.

### Todos os métodos expostos pelo RPCDispatcher

| Metodo | Handler | Parametros | Retorno resumido |
|--------|---------|-----------|----------------|
| `register_node` | `NodeRegistry.register_node` | `node_id`, `ip` (injetado pelo servidor) | `None` (resultado vazio indica sucesso) |
| `heartbeat` | `NodeRegistry.heartbeat` | `node_id` | `None` |
| `get_active_nodes` | `NodeRegistry.get_active_nodes` | - | lista de `{node_id, ip, status, last_seen}` |
| `add_clicks` | `GameManager.add_clicks` | `node_id`, `clicks`, `lamport_ts` | `{status, accepted_clicks, rejected_clicks, global_score, node_score, lamport_ts, milestone, milestone_value}` |
| `sync_offline` | `GameManager.sync_offline` | `node_id`, `accumulated_clicks`, `lamport_ts` | mesmo que `add_clicks` + `local_score`, `node_scores` |
| `activate_powerup` | `GameManager.activate_powerup` | `node_id` | `{status, time_remaining}` ou `{error: "ALREADY_ACTIVE"}` |
| `get_nodes_scores` | `GameRepository.get_nodes_scores` | - | lista de `{node_id, local_score}` ordenada por score DESC |
| `set_processing_delay` | `RPCDispatcher.set_processing_delay` | `delay_ms` | `{status, new_delay}` |

### Arquivos de referência
- `firmware/rpc_client.c` - implementacao do cliente
- `server/infra/rpc_server.py` - `RPCDispatcher` e `handle_client`

### Como demonstrar
Abrir um terminal e enviar um JSON-RPC manualmente:
```bash
echo '{"jsonrpc":"2.0","method":"get_nodes_scores","params":{},"id":99}' | nc <IP> 8765
```
A resposta mostra o servidor respondendo ao método nomeado independente de quem chamou.

---

## 2. Concorrência Event-Driven

### Requisito conceitual
Um servidor orientado a eventos atende múltiplas conexões com uma única thread via _cooperative multitasking_: uma operação I/O suspende a corrotina atual e cede o controle ao event loop, que executa outra corrotina pronta.

### Implementação no projeto

O servidor usa `asyncio` Python. A funcao `asyncio.start_server()` em `start_rpc_server()` registra `handle_client` como corrotina de conexao. Para cada cliente TCP, o event loop cria uma corrotina independente:

```python
# rpc_server.py
server = await asyncio.start_server(
    lambda r, w: handle_client(r, w, dispatcher),
    host, port,
)
```

Em `handle_client`, `await reader.readline()` suspende a corrotina enquanto aguarda dados, liberando o event loop para atender outros clientes. As operacoes de banco de dados (`aiosqlite`) tambem sao `await`, entao nenhum I/O bloqueia o event loop.

### Por que asyncio.Lock mesmo sem threads

O `GameManager` atualiza `global_score` e `node["score"]` em varias etapas. Sem lock, duas corrotinas poderiam intercalar entre `node["score"] += actual_clicks` e `await game_repo.update_score(...)`, ja que cada `await` e um ponto de preempcao. O `game_lock = asyncio.Lock()` (injetado no construtor) garante que `_add_clicks_logic` e `_sync_offline_logic` executem como secao critica:

```python
async with self.lock:
    return await self._add_clicks_logic(...)
```

### SIMULATE_PROCESSING_DELAY_MS

Definida em `server/app/config.py`, injeta um `await asyncio.sleep(delay_ms / 1000)` apos cada dispatch bem-sucedido (`rpc_server.py`, linha 103). A corrotina do cliente A dorme, mas o event loop continua atendendo B e C normalmente. Com 3 placas simultâneas o log mostra `active_connections=3`.

Tambem pode ser alterada em tempo real via RPC: `set_processing_delay(delay_ms=500)`.

### Arquivos de referência
- `server/infra/rpc_server.py` - `handle_client`, `RPCDispatcher.dispatch`
- `server/app/config.py` - `SIMULATE_PROCESSING_DELAY_MS`
- `server/domain/game_manager.py` - `async with self.lock`

### Como demonstrar
```bash
cd server && SIMULATE_PROCESSING_DELAY_MS=500 python -m app.main
```
Com 3 placas enviando cliques, o log mostrara `active_connections=3` e cada chamada levara ~500ms, mas todas serao processadas concorrentemente (sem fila de espera serial).

---

## 3. Tolerância a Falhas - Circuit Breaker

### Requisito conceitual
O Circuit Breaker monitora falhas consecutivas em uma dependencia remota. Apos atingir um limiar o circuito abre e deixa de chamar a dependencia, evitando cascata. Apos um tempo tenta reconectar.

### Diagrama de estados

```
                    3 falhas consecutivas (RPC_TIMEOUT ou RPC_DISCONNECTED)
  +--------+     ----------------------------------------------------->  +---------+
  | CLOSED |                                                             |  OPEN   |
  | online |     <-----------------------------------------------------  | offline |
  +--------+     rpc_register_node() sucesso + sync_offline OK           +---------+
                                                                              |
                                                                    backoff exponencial
                                                                    1->2->5->10s
                                                                         |
                                                                    +----------+
                                                                    | HALF-    |
                                                                    | OPEN     |
                                                                    | (retry)  |
                                                                    +----------+
```

### Mapeamento estado -> variaveis do firmware

| Estado Circuit Breaker | `connection_status_t` | `registered` | `consecutive_rpc_failures` |
|------------------------|----------------------|--------------|---------------------------|
| CLOSED | `STATUS_ONLINE` | `true` | 0 |
| OPEN | `STATUS_OFFLINE` | `false` | 0 (resetado ao abrir) |
| HALF-OPEN (tentando) | `STATUS_CONNECTING` | `false` | 0..N |
| SYNCING | `STATUS_SYNCING` | `true` (recém registrado) | 0 |

O circuito abre quando `consecutive_rpc_failures >= 3` (verificado apos cada ciclo de 20ms e apos heartbeat). Violacoes de Lamport e rate limit **nao** incrementam o contador - apenas falhas de rede e timeout.

### Heartbeat como mecanismo complementar

A cada 30 segundos (`HEARTBEAT_INTERVAL_US = 30000000`) enquanto `STATUS_ONLINE`, o firmware envia `rpc_register_node()` como heartbeat. Uma falha no heartbeat incrementa `consecutive_rpc_failures`. O servidor, independentemente, marca nos como `INACTIVE` apos 60 segundos sem atividade (tarefa `background_tasks()` rodando a cada 10 segundos).

### Arquivos de referência
- `firmware/tasks/task_rpc.c` - logica completa do circuit breaker e heartbeat
- `firmware/config/firmware_config.h` - `RPC_REGISTER_BACKOFF_*`, `HEARTBEAT_INTERVAL_US`
- `server/domain/node_registry.py` - `mark_inactive()`: timeout de 60s

### Como demonstrar
1. Iniciar servidor + 3 placas. Aguardar STATUS_ONLINE.
2. Desligar o servidor.
3. Observar no log da placa: `Falha RPC #1/3`, `#2/3`, `#3/3` → `STATUS_OFFLINE`.
4. Observar no log do servidor apos ser religado: `INACTIVE` apos 60s sem heartbeat.
5. Religar servidor → placa faz retry exponencial → `STATUS_SYNCING` → `STATUS_ONLINE`.

---

## 4. Sincronização - Relógio de Lamport

### Requisito conceitual
O relógio de Lamport atribui timestamps a eventos sem precisar de clock global. Se o evento A causou B, entao `L(A) < L(B)`. As regras sao:
1. Antes de enviar: `L = L + 1`
2. Ao receber com timestamp `T`: `L = max(L, T) + 1`

### Implementação no projeto

**Firmware (lamport.c)**:

- `lamport_tick()`: `L = L + 1`; retorna novo L; chamado antes de cada envio RPC
- `lamport_update(received_ts)`: `L = max(L, received_ts) + 1`; chamado apos resposta bem-sucedida
- Estado `lamport_L` protegido pelo mesmo spinlock de `shared_state`
- Overflow detectado em `LAMPORT_MAX_SAFE = UINT32_MAX - 1000`: reset para 1 com log

**Servidor (lamport_clock.py)**:

- `update(node_id, received_ts)`: `lamport = max(self.local_lamport, received_ts) + 1`
- `get_last_by_node(node_id)`: retorna `last_lamport` do no

**Deteccao de violacao (game_manager.py)**:

```python
last_ts = await self.lamport_clock.get_last_by_node(node_id)
if lamport_ts <= last_ts:
    # VIOLACAO: servidor rejeita e persiste para auditoria
    curr_lamport = await self.lamport_clock.update(node_id, lamport_ts)
    await self.game_repo.insert_lamport_violation(node_id, lamport_ts, last_ts)
    return {"error": "LAMPORT_VIOLATION", "lamport_ts": curr_lamport}
```

**Firmware ao receber violacao (`parse_add_clicks_response`)**:
```c
if (json_contains(json, "LAMPORT_VIOLATION")) {
    result.error_code = RPC_LAMPORT_VIOLATION;
    json_get_int(json, "lamport_ts", &temp_lamport);
    result.lamport_ts = (uint32_t)temp_lamport;
}
```
Entao `task_rpc.c` chama `lamport_update(click_res.lamport_ts)` para corrigir o clock local. Violacao de Lamport **nao** conta como falha de rede no circuit breaker.

### Exemplo de sequência de mensagens

```
Firmware Node 0          Servidor
     L=0                   L_server=0
     |
     | lamport_tick() -> L=1
     |-- add_clicks(lamport_ts=1) -->
                           L_server = max(0, 1) + 1 = 2
                           last_0 = 2
                   <-- {lamport_ts: 2} --
     | lamport_update(2) -> L = max(1, 2) + 1 = 3
     |
     | lamport_tick() -> L=4
     |-- add_clicks(lamport_ts=4) -->
                           last_0=2; 4 > 2 => OK
                           L_server = max(2, 4) + 1 = 5
                   <-- {lamport_ts: 5} --
     | lamport_update(5) -> L = max(4, 5) + 1 = 6
     |
     | [packet perdido e reenviado com L=4]
     |-- add_clicks(lamport_ts=4) -->
                           last_0=5; 4 <= 5 => VIOLACAO
                           insert_lamport_violation(received=4, server=5)
                   <-- {error: LAMPORT_VIOLATION, lamport_ts: 6} --
     | lamport_update(6) -> L = max(6, 6) + 1 = 7
```

### Arquivos de referência
- `firmware/middleware/lamport.c/.h`
- `server/domain/lamport_clock.py`
- `server/domain/game_manager.py` - `_add_clicks_logic` (linha 91-105)
- `server/infra/db.py` - tabela `lamport_violations`

### Como demonstrar
Usar `test_rpc.py` ou `nc` para enviar `add_clicks` com `lamport_ts` decrescente. O servidor rejeita e o log mostra `[LAMPORT] VIOLAÇÃO causal detectada`. O endpoint `/api/violations` da dashboard retorna o historico.

---

## 5. Descoberta de Serviços - UDP Broadcast

### Requisito conceitual
Os nos nao precisam conhecer previamente o endereco do servidor. A transparencia de localizacao permite que o servidor mude de IP sem recompilacao do firmware.

### Protocolo implementado

**Firmware envia (broadcast IPv4, porta 9999):**
```
COOKIE_DISCOVER:NODE_ID:<N>
```
Exemplo: `COOKIE_DISCOVER:NODE_ID:0`

**Servidor responde (para o endereco de origem):**
```
COOKIE_SERVER:<porta_rpc>
```
Exemplo: `COOKIE_SERVER:8765`

**Fluxo:**
1. `service_disc_discover()` chama `udp_new()`, `udp_bind(IP_ANY_TYPE, 0)`, `udp_recv(callback)`
2. Envia `udp_sendto(pcb, pbuf, IP_ADDR_BROADCAST, 9999)`
3. Loop de polling com `sleep_ms(10)` ate `timeout_deadline` (3 segundos passados por `setup_network_target()`)
4. Callback `udp_recv_callback` valida prefixo `COOKIE_SERVER:`, parseia porta, seta `g_disc_success=true`
5. Se sucesso: `rpc_client_set_server(ip_descoberto, porta)`, `fallback_in_use=false`
6. Se timeout: `rpc_client_set_server_fallback()` usa `FALLBACK_SERVER_IP` (padrao `192.168.0.10`, sobrescrito em compilacao via `-DFALLBACK_SERVER_IP=...`)

**Servidor (udp_discovery.py):**
Valida que a mensagem comeca com `COOKIE_DISCOVER` e tem formato `partes[1] == "NODE_ID"` antes de responder.

### Por que isso e transparencia de localizacao
O firmware descobre o IP dinamicamente. Se o servidor trocar de IP, basta estar na mesma subnet de broadcast; nao precisa regravar o firmware.

### Arquivos de referência
- `firmware/discovery/service_disc.c/.h` - cliente UDP
- `server/infra/udp_discovery.py` - servidor UDP
- `firmware/tasks/task_rpc.c` - `setup_network_target()`

---

## 6. Consistência Eventual - Modelo Offline-First

### Requisito conceitual
Com consistencia eventual as operacoes continuam localmente durante particoes de rede. Ao reconectar os estados sao reconciliados e o sistema converge sem perder operacoes validas.

### Implementação no projeto

**Escrita local durante particao:**
- ISR dos botoes chama `shared_state_increment_pending_clicks()` (spinlock)
- `task_buttons` e `task_rpc` detectam `STATUS_OFFLINE` e chamam `shared_state_restore_clicks(pending)` em vez de enviar pela rede
- Cliques ficam acumulados em `state.pending_clicks`

**Reconciliacao ao reconectar (`_sync_offline_logic`):**
```python
offline_seconds = max(0, now - last_seen)  # tempo desde ultimo heartbeat no BD
max_allowed = int(RATE_LIMIT * offline_seconds)  # 50 clicks/s x tempo offline
accepted    = min(accumulated_clicks, max_allowed)
rejected    = accumulated_clicks - accepted
```
- Se `last_seen == 0` (no nunca conectou): `offline_seconds = 0`, `max_allowed = 0`, `accepted = 0`
- Se `offline_seconds > 0`: aceita proporcional ao tempo de ausencia

**Garantias do modelo:**
1. Cliques acumulados ficam na RAM sob spinlock ate a reconexao; nenhum e descartado enquanto a placa estiver ligada
2. Rate limiting proporcional ao tempo offline: aceitos <= 50 clicks/s * segundos_offline
3. Apos sync_offline o servidor atualiza `local_score` no banco e retorna o estado consolidado

**Atencao:** os cliques pendentes vivem so na RAM. Se a placa perder energia os dados sao perdidos; nao ha armazenamento persistente no firmware.

### Arquivos de referência
- `firmware/middleware/shared_state.c` - `shared_state_restore_clicks`, `shared_state_take_pending_clicks`
- `firmware/tasks/task_rpc.c` - bloco offline e sync apos registro
- `server/domain/game_manager.py` - `_sync_offline_logic`

### Como demonstrar
1. Com no ONLINE, gerar cliques.
2. Desligar servidor (placa va para OFFLINE, cliques acumulam em `pending_clicks`).
3. Gerar mais cliques offline.
4. Religar servidor.
5. Observar no log do servidor: `sync_offline_inicio`, `rate_limit_sync` ou `sync_aceita_tudo`, `sync_offline_ok`.
6. O dashboard reflete `SYNCING` → `ACTIVE` com score atualizado.
