# PRESENTATION_GUIDE.md — Guia de Apresentação

---

## Seção 1: Visão Geral para o Avaliador

O `clicker-rp2040` é um sistema distribuído de cliques em tempo real composto por três nós físicos (BitDogLab, baseada no RP2040 / Raspberry Pi Pico W) e um servidor central em Python. Cada nó registra pressionamentos do botão A e envia os cliques periodicamente ao servidor via JSON-RPC 2.0 sobre TCP. O servidor consolida os scores, valida a ordem causal dos eventos com relógio de Lamport e notifica um dashboard web em tempo real via WebSocket.

Se um nó perder a conexão WiFi ou o servidor cair, o firmware acumula os cliques localmente na RAM (protegidos por spinlock de hardware) e os envia ao reconectar via o método `sync_offline`. O servidor aplica rate limiting proporcional ao tempo de ausência para evitar injeção retroativa irrestrita. O sistema converge para um estado consistente sem perder eventos válidos.

A comunicação de rede é completamente encapsulada no módulo `firmware/rpc_client.c`. Nenhuma outra parte do firmware conhece lwIP ou estruturas de socket. No servidor, a separação em três camadas (dashboard, domínio, infraestrutura) segue o padrão N-camadas com imports estritamente unidirecionais.

As próximas seções detalham onde cada requisito está implementado e como demonstrá-lo durante a apresentação.

---

## Seção 2: Requisito — Comunicação via RPC (Middleware)

### 2.1 O que o requisito pede

Uso de comunicação via RPC/RMI. O cliente não deve gerenciar sockets; a comunicação deve ser mediada por um middleware transparente.

### 2.2 Como está implementado

O middleware de RPC reside integralmente em `firmware/rpc_client.c`. O restante do firmware (tasks de botão, display, monitor) invoca operações remotas como se fossem chamadas de função C locais, sem qualquer conhecimento de TCP, lwIP ou estrutura de socket:

```c
RpcClickResult  rpc_add_clicks(int clicks, uint32_t lamport_ts);
RpcClickResult  rpc_sync_offline(int accumulated_clicks, uint32_t lamport_ts);
RpcSimpleResult rpc_register_node(uint8_t node_id);
RpcPowerupResult rpc_activate_powerup(void);
RpcScoreResult  rpc_get_scores(void);
```

Cada função serializa a requisição em JSON-RPC 2.0, envia via TCP (com retry e backoff internos) e parseia a resposta, retornando uma struct tipada com código de erro semântico.

No servidor, a classe `RPCDispatcher` em `server/infra/rpc_server.py` recebe conexões TCP, parseia o JSON e despacha para o handler registrado conforme o campo `"method"`. Os handlers são métodos de domínio (`GameManager`, `NodeRegistry`, `GameRepository`) — o dispatcher não contém lógica de negócio.

### 2.3 Evidências no código

- `firmware/rpc_client.c` — assinaturas das 6 funções RPC (linhas ~30-40)
- `firmware/tasks/task_rpc.c` — chamadas a `rpc_add_clicks()` sem nenhum `#include` de `lwip/sockets.h`
- `server/infra/rpc_server.py` — `RPCDispatcher.dispatch()` e dicionário `handlers`
- `docs/API_RPC.md` — schema completo de todos os 8 métodos com exemplos JSON

### 2.4 Como demonstrar ao vivo

1. Abrir `firmware/tasks/task_rpc.c` e mostrar a chamada `rpc_add_clicks(pending, lamport_ts)` sem nenhum include de sockets.
2. Enviar requisição manual:
```bash
echo '{"jsonrpc":"2.0","method":"get_nodes_scores","params":{},"id":99}' | nc localhost 8765
```
3. Mostrar resposta JSON e explicar que o servidor retornou dados tipados sem o cliente precisar saber onde o socket está aberto.

### 2.5 Perguntas esperadas

**P: Como o servidor identifica qual método chamar?**
O campo `"method"` no JSON mapeia para uma entrada no dicionário `handlers` do `RPCDispatcher`. Se o método não existir, retorna erro `-32601 Method not found`. Ver `rpc_server.py` linha ~25.

**P: O JSON-RPC aqui é síncronos ou assíncrono?**
Síncrono do ponto de vista do firmware: `rpc_call_with_retry()` bloqueia até receber resposta ou atingir timeout. Do lado do servidor é assíncrono: a corrotina `handle_client` suspende no `await reader.readline()` sem bloquear outras conexões.

---

## Seção 3: Requisito — Arquitetura N-Camadas

### 3.1 O que o requisito pede

Organização em camadas: Apresentação, Lógica de Negócios, Persistência. Cada camada deve comunicar-se apenas com a camada imediatamente abaixo.

### 3.2 Como está implementado

O servidor segue separação estrita em três camadas, refletida na estrutura de diretórios:

| Camada | Diretório | Conteúdo |
|--------|-----------|---------|
| Apresentação | `server/dashboard/` | HTTP `:8080`, WebSocket `:8081`, HTML/CSS/JS |
| Lógica de Negócio | `server/domain/` | `GameManager`, `NodeRegistry`, `LamportClock`, `GameRepository` |
| Persistência | `server/infra/` | `db.py` (SQLite via aiosqlite), `logger.py`, `rpc_server.py` |

Os imports são unidirecionais: `dashboard` importa `domain`; `domain` importa `infra`; `infra` não importa `domain` nem `dashboard`. A comunicação inversa (domínio notificando o dashboard) usa injeção de dependência via `set_notifier()`, sem import circular.

### 3.3 Evidências no código

- `server/dashboard/state_service.py` — importa `db` (infra) e recebe `GameManager` (domain) via construtor
- `server/domain/game_manager.py` — linha 4: importa `infra.logger`; linha 21: `self._event_notifier = None` (dashboard injetado)
- `server/infra/db.py` — nenhum import de `domain` ou `dashboard`

### 3.4 Como demonstrar ao vivo

Abrir o explorador de arquivos e mostrar a estrutura `server/domain/`, `server/infra/`, `server/dashboard/`. Rodar:
```bash
grep -r "from domain" server/infra/   # sem resultados
grep -r "from dashboard" server/domain/  # sem resultados
```

### 3.5 Perguntas esperadas

**P: Por que o dispatcher RPC está em `infra` e não em `domain`?**
O dispatcher é infraestrutura de protocolo (aceita conexões TCP, parseia JSON-RPC, serializa respostas). A lógica de negócio fica nos handlers que ele chama. É a mesma separação de um controller MVC.

---

## Seção 4: Requisito — Concorrência Event-Driven

### 4.1 O que o requisito pede

Servidor capaz de atender múltiplos clientes simultaneamente, usando multithreading ou modelo event-driven.

### 4.2 Como está implementado

O servidor usa `asyncio` com event loop single-threaded. Para cada conexão TCP aceita, `asyncio.start_server()` cria uma corrotina `handle_client` independente. O ponto `await reader.readline()` suspende a corrotina e libera o event loop para processar outras conexões — nenhuma thread adicional é criada.

O `GameManager` usa `asyncio.Lock` para proteger a seção crítica de atualização de `global_score`: sem lock, duas corrotinas intercalariam entre `node["score"] += x` e `await db.update_score()`, causando inconsistência de dados.

A variável `active_connections` em `rpc_server.py` é incrementada/decrementada a cada conexão/desconexão e incluída nos logs, tornando a concorrência diretamente observável.

### 4.3 Evidências no código

- `server/infra/rpc_server.py` — `handle_client` como `async def`; contador `active_connections`
- `server/domain/game_manager.py` — `async with self.lock` antes de `_add_clicks_logic`
- `server/app/config.py` — `SIMULATE_PROCESSING_DELAY_MS`

### 4.4 Como demonstrar ao vivo

1. Iniciar servidor:
```bash
cd server && SIMULATE_PROCESSING_DELAY_MS=500 python -m app.main
```
2. Com 3 placas enviando cliques, o log mostrará:
```json
{"tag":"[RPC]","msg":"dispatch_ok","method":"add_clicks","active_connections":3}
```
3. Cada chamada leva ~500ms, mas todas são processadas concorrentemente (não em fila serial).

### 4.5 Perguntas esperadas

**P: Por que asyncio.Lock se não há threads?**
Cada `await` é um ponto de preempção cooperativa. Sem lock, corrotina A poderia incrementar `global_score` e ser preemptada no `await db.update_score()`, momento em que corrotina B leria o valor desatualizado. Ver `game_manager.py` linha ~82.

**P: Qual o limite de conexões simultâneas?**
Limitado pelo número de corrotinas que o event loop suporta e pelos file descriptors do sistema. Para 3 nós físicos não há limite prático.

---

## Seção 5: Requisito — Tolerância a Falhas (Circuit Breaker)

### 5.1 O que o requisito pede

Retries automáticos ou implementação do padrão Circuit Breaker para tolerar falhas na comunicação entre nós.

### 5.2 Como está implementado

O firmware implementa os três estados clássicos do Circuit Breaker em `firmware/tasks/task_rpc.c`:

- **CLOSED** (`STATUS_ONLINE`, `consecutive_rpc_failures == 0`): operação normal, envia cliques a cada 20ms.
- **OPEN** (`STATUS_OFFLINE`, `registered = false`): após 3 falhas consecutivas de rede, o circuito abre. Cliques são devolvidos ao `shared_state` e acumulam em RAM.
- **HALF-OPEN** (retentativas com backoff): tenta `rpc_register_node()` com backoff exponencial 1s → 2s → 5s → 10s. Sucesso fecha o circuito; falha reinicia o backoff.

Apenas erros de rede (timeout, socket fechado) contam para o contador. Violações de Lamport e rate limiting não contam — são respostas válidas do servidor.

O servidor complementa com detecção passiva: `mark_inactive()` marca nós sem heartbeat há mais de 60s como `INACTIVE`, rodando a cada 10s em background.

### 5.3 Evidências no código

- `firmware/tasks/task_rpc.c` — `if (consecutive_rpc_failures >= 3)` (linha ~409), backoff no bloco `!registered`
- `firmware/config/firmware_config.h` — `RPC_REGISTER_BACKOFF_1/2/3/MAX`
- `server/domain/node_registry.py` — `mark_inactive()`, timeout de 60s (linha ~91)

### 5.4 Como demonstrar ao vivo

1. Com 3 placas em STATUS_ONLINE, parar o servidor (`Ctrl+C`).
2. Log serial da placa: `Falha RPC #1/3` → `#2/3` → `#3/3` → display mostra `!! OFFLINE`.
3. Gerar cliques offline — display mostra contagem acumulando.
4. Religar servidor.
5. Placa faz retry → log serial mostra backoff → `STATUS_SYNCING` → `STATUS_ONLINE`.
6. Log do servidor mostra `sync_offline_ok` com score atualizado.

### 5.5 Perguntas esperadas

**P: E se a placa perder energia enquanto offline?**
Cliques pendentes são perdidos — vivem apenas na RAM. É trade-off consciente: writes em flash degradam a memória do RP2040 e o volume de cliques não justifica persistência.

**P: Circuit breaker no firmware é diferente do padrão clássico?**
Não: CLOSED, OPEN e HALF-OPEN estão implementados. A diferença é que não há timeout fixo para fechar — o circuito fecha somente após `register_node()` bem-sucedido e sync offline. Ver `task_rpc.c` linhas ~409-420.

---

## Seção 6: Requisito — Sincronização (Relógio de Lamport)

### 6.1 O que o requisito pede

Mecanismo de sincronização entre nós: relógios lógicos ou exclusão mútua distribuída.

### 6.2 Como está implementado

Cada mensagem RPC carrega um campo `lamport_ts`. O firmware incrementa o clock antes de cada envio (`lamport_tick()`) e atualiza ao receber resposta (`lamport_update(received_ts)` → `L = max(L, received_ts) + 1`). Ambas as funções são protegidas pelo mesmo spinlock de hardware do `shared_state`.

O servidor mantém `last_lamport` por nó em `LamportClock` e detecta violações causais em `_add_clicks_logic`:

```python
last_ts = await self.lamport_clock.get_last_by_node(node_id)
if lamport_ts <= last_ts:
    await self.game_repo.insert_lamport_violation(node_id, lamport_ts, last_ts)
    return {"error": "LAMPORT_VIOLATION", "lamport_ts": curr_lamport}
```

Violações são persistidas em `lamport_violations` no SQLite para auditoria e acessíveis via `/api/violations`.

### 6.3 Evidências no código

- `firmware/middleware/lamport.c` — `lamport_tick()` e `lamport_update()` com overflow detection
- `server/domain/game_manager.py` — detecção e rejeição (linhas 91-105)
- `server/domain/lamport_clock.py` — `update()`: `max(local, received) + 1`
- `server/infra/db.py` — tabela `lamport_violations` (linha ~68)

### 6.4 Como demonstrar ao vivo

Enviar requisição com timestamp regressivo:
```bash
echo '{"jsonrpc":"2.0","method":"add_clicks","params":{"node_id":0,"clicks":5,"lamport_ts":1},"id":99}' | nc localhost 8765
```
Resposta esperada:
```json
{"jsonrpc":"2.0","result":{"error":"LAMPORT_VIOLATION","lamport_ts":42},"id":99}
```
Abrir `http://<IP>:8080/api/violations` — violação registrada com `received_ts=1`, `server_ts=N`.

### 6.5 Perguntas esperadas

**P: Qual a diferença entre Lamport e timestamp físico?**
Lamport captura causalidade: se A causou B, então `L(A) < L(B)`. Dois eventos com `L=5` e `L=7` podem ter ocorrido no mesmo segundo físico — o que importa é a relação de causa e efeito, não o tempo de parede.

**P: O que acontece com o clock do firmware ao receber LAMPORT_VIOLATION?**
O servidor retorna o `lamport_ts` corrigido no campo de erro. O firmware chama `lamport_update(resp.lamport_ts)` para sincronizar o clock local antes de tentar novamente. A violação não conta para o circuit breaker.

---

## Seção 7: Requisito — Descoberta de Serviços (UDP Broadcast)

### 7.1 O que o requisito pede

Nó de nomes ou mecanismo de localização dinâmica de serviços, para que os clientes não precisem conhecer o endereço do servidor a priori.

### 7.2 Como está implementado

Ao inicializar, cada nó envia um UDP broadcast na porta 9999:
```
COOKIE_DISCOVER:NODE_ID:<N>
```
O servidor, ao receber, responde diretamente ao remetente:
```
COOKIE_SERVER:8765
```
O firmware extrai IP de origem e porta da resposta e configura o cliente RPC (`rpc_client_set_server(ip, porta)`). Se o discovery falhar em 3 segundos, usa o `FALLBACK_SERVER_IP` compilado no binário via `-DFALLBACK_SERVER_IP=...` no CMake.

### 7.3 Evidências no código

- `firmware/discovery/service_disc.c` — `service_disc_discover()`: UDP sendto + polling de callback
- `server/infra/udp_discovery.py` — `DiscoveryDatagramProtocol.datagram_received()`: valida prefixo e responde
- `firmware/tasks/task_rpc.c` — `setup_network_target()`: chama discover e configura fallback
- `firmware/rpc_client.c` — `FALLBACK_SERVER_IP` com guard `#ifndef`

### 7.4 Como demonstrar ao vivo

1. Iniciar servidor — porta 9999 já está escutando.
2. Ligar uma placa — log serial mostra:
```
[DISC] Broadcast enviado: COOKIE_DISCOVER:NODE_ID:0. Aguardando UDP...
[DISC] Sucesso! Servidor em 192.168.0.5:8765
```
3. Argumento: se o IP do servidor mudar (ex: DHCP), a placa descobre o novo IP automaticamente sem regravar firmware.

### 7.5 Perguntas esperadas

**P: E se a placa estiver em rede diferente do servidor?**
UDP broadcast não atravessa roteadores. Nesse caso discovery falha e o firmware usa o `FALLBACK_SERVER_IP`. O IP de fallback pode ser sobrescrito em compile time sem alterar o código-fonte.

**P: Discovery acontece uma só vez?**
Sim, no boot. Após discovery bem-sucedido (ou fallback), o IP é fixo na sessão. Se o servidor trocar de IP, a placa continua tentando o IP antigo até perder conectividade e reiniciar.

---

## Seção 8: Requisito — Consistência Eventual (Sincronização Offline)

### 8.1 O que o requisito pede

Se houver replicação ou operação particionada, demonstrar como a consistência é alcançada após reconexão.

### 8.2 Como está implementado

Durante partição de rede (STATUS_OFFLINE), a ISR do botão A continua incrementando `pending_clicks` em `shared_state` via spinlock. O firmware não descarta nenhum clique — apenas deixa de enviá-los.

Ao reconectar, o firmware chama `sync_offline(accumulated_clicks, lamport_ts)`. O servidor aplica rate limiting proporcional ao tempo de ausência:

```python
offline_seconds = max(0, now - last_seen)
max_allowed = int(50 * offline_seconds)   # 50 clicks/s
accepted    = min(accumulated_clicks, max_allowed)
```

Se o nó nunca tinha se registrado (`last_seen == 0`), `offline_seconds = 0` e nenhum clique é aceito — evita injeção irrestrita no primeiro sync. Após o sync, o servidor retorna o estado consolidado com `node_scores` de todos os nós e o nó volta para STATUS_ONLINE.

### 8.3 Evidências no código

- `firmware/middleware/shared_state.c` — `shared_state_restore_clicks()` e `shared_state_take_pending_clicks()`
- `server/domain/game_manager.py` — `_sync_offline_logic()` (linhas ~220-342)
- `firmware/tasks/task_rpc.c` — bloco de sync após registro bem-sucedido

### 8.4 Como demonstrar ao vivo

1. Com placa ONLINE, gerar alguns cliques (score sobe no dashboard).
2. Parar o servidor (`Ctrl+C`) — placa vai para OFFLINE em ~2s.
3. Gerar 30 cliques offline — display mostra acumulando.
4. Religar servidor.
5. Log do servidor:
```
{"msg":"sync_offline_inicio","node":0,"accumulated":30}
{"msg":"sync_aceita_tudo","node":0,"accepted":30,"offline_s":15.2}
{"msg":"sync_offline_ok","node":0,"global_score":N}
```
6. Dashboard atualiza score sem perda de cliques válidos.

### 8.5 Perguntas esperadas

**P: Por que rate limiting proporcional ao tempo offline?**
Sem ele, um nó poderia ficar offline por horas, acumular 100.000 cliques e injetá-los instantaneamente, distorcendo o placar. O limite de `50 clicks/s × offline_seconds` reflete o máximo fisicamente possível no período de ausência.

**P: Score é replicado entre nós?**
Não há replicação entre nós. O servidor é o único ponto de verdade. Cada nó mantém um score local na RAM (atualizado pelo servidor via resposta RPC), mas é apenas cache de display — o servidor decide o score real.

---

## Seção 9: Mapeamento Requisitos → Evidências

| Requisito | Arquivo principal | Função/Linha | Demonstração |
|-----------|------------------|-------------|--------------|
| RPC | `firmware/rpc_client.c` | `rpc_add_clicks()` L~30 | Enviar JSON via `nc` |
| N-Camadas | `server/domain/`, `server/infra/` | Imports unidirecionais | `grep -r "from domain" server/infra/` |
| Concorrência | `server/infra/rpc_server.py` | `active_connections` L~15 | Log `active_connections=3` |
| Circuit Breaker | `firmware/tasks/task_rpc.c` | `consecutive_rpc_failures >= 3` L~409 | Desligar servidor → `!! OFFLINE` |
| Lamport | `server/domain/game_manager.py` | `lamport_ts <= last_ts` L~92 | Requisição com ts regressivo |
| Discovery | `firmware/discovery/service_disc.c` | `service_disc_discover()` | Log serial `DISC: Sucesso!` |
| Consistência Eventual | `server/domain/game_manager.py` | `_sync_offline_logic()` L~220 | Cliques offline → sync → dashboard |

---

## Seção 10: Checklist de Execução

```
[ ] 1. Build limpo:
        rm -rf firmware/build firmware/dist && ./firmware/build_all.sh
        Verificar: 3 .uf2 em firmware/dist/ com SHA256 distintos

[ ] 2. Gravar placas (etiqueta física = arquivo):
        NODE-0 → avocado_node0.uf2
        NODE-1 → avocado_node1.uf2
        NODE-2 → avocado_node2.uf2

[ ] 3. Iniciar servidor:
        cd server
        SIMULATE_PROCESSING_DELAY_MS=500 python -m app.main
        Verificar: "AbacateOS - SERVIDOR INICIADO" + IP local no stdout

[ ] 4. Ligar 3 placas:
        Verificar: log do servidor mostra register_novo node=0, node=1, node=2

[ ] 5. Confirmar dashboard:
        http://<IP>:8080
        Verificar: 3 nós ACTIVE visíveis

[ ] 6. Demonstrar concorrência:
        Pressionar botões das 3 placas por ~2s
        Verificar: log mostra active_connections=3 em múltiplas linhas

[ ] 7. Demonstrar circuit breaker:
        Ctrl+C no servidor
        Verificar: placas exibem "!! OFFLINE" em ~2s
        Religar servidor
        Verificar: log mostra SYNCING → ACTIVE

[ ] 8. Demonstrar Lamport:
        echo '{"jsonrpc":"2.0","method":"add_clicks","params":{"node_id":0,"clicks":5,"lamport_ts":1},"id":99}' | nc localhost 8765
        Verificar: resposta contém "LAMPORT_VIOLATION"
        http://<IP>:8080/api/violations — violação persistida

[ ] 9. Demonstrar sync offline:
        Ctrl+C no servidor
        Gerar cliques em uma placa
        Religar servidor
        Verificar: log mostra sync_offline_ok + score atualizado no dashboard
```

---

## Seção 11: Perguntas Difíceis e Respostas

**P: Por que não usar threads no servidor?**
Threads têm overhead de context switch e exigem locks mais pesados. Com asyncio, o overhead por conexão é uma corrotina de ~1 KB de stack. Para I/O-bound (sockets, banco de dados) o event loop é suficiente e mais simples de raciocinar. Ver `rpc_server.py` — sem `threading.Thread` em nenhuma linha.

**P: O que acontece se dois nós enviarem cliques no mesmo instante?**
Timestamps de Lamport garantem ordem: cada nó incrementa antes de enviar, então `L_0 ≠ L_1` mesmo que o envio seja simultâneo. O servidor processa sequencialmente no event loop, e o `game_lock` garante que `global_score` não seja atualizado concorrentemente. Ver `game_manager.py` L~82.

**P: E se o banco SQLite corromper?**
O score global é reconstruído do banco no boot via `reconstruct_global_score()`. Deletar `avocado.db` faz o servidor recriar o schema vazio — sistema continua operando com scores zerados. Para produção usaríamos PostgreSQL com replicação; SQLite é adequado para prototipação acadêmica.

**P: Circuit breaker no firmware é o padrão clássico?**
Sim: CLOSED (online, 0 falhas), OPEN (offline, após 3 falhas), HALF-OPEN (retentativas com backoff). A diferença do padrão clássico é que não há timeout fixo para fechar o circuito — ele fecha apenas após `register_node()` bem-sucedido mais sync offline. Ver `task_rpc.c` L~409.

**P: O relógio de Lamport garante ordem total entre todos os nós?**
Não — Lamport garante apenas ordem causal (se A causou B, então `L(A) < L(B)`). Dois eventos concorrentes podem ter qualquer relação de timestamps. Para ordem total precisaríamos de Lamport com desempate por ID de nó (Lamport total order) ou vector clocks. O projeto usa Lamport para detectar pacotes atrasados ou duplicados, não para ordenação global de eventos.

**P: Por que o dashboard usa WebSocket e não polling HTTP?**
O servidor já tem event loop asyncio. WebSocket é nativo nesse modelo: o domínio chama `_notify()` que faz broadcast imediato sem armazenar em fila. Com polling HTTP, o dashboard descobriria eventos com latência de `POLL_INTERVAL`. Ver `dashboard/websocket.py` — `notify()` e `broadcast_loop()` (safety net a cada 5s).
