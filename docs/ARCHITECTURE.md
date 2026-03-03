# ARCHITECTURE.md — Arquitetura do Sistema Clicker RP2040

## Visão Geral do Sistema

```
+-----------+       TCP 8765         +---------------+
| Firmware  |  JSON-RPC 2.0 over TCP |               |
| Node 0    |----------------------->|               |
|           |                        |  RPCDispatcher|
+-----------+                        |               |
                                     |  GameManager  |      SQLite
+-----------+       TCP 8765         |  NodeRegistry |----> avocado.db
| Firmware  |----------------------->|  GameRepo     |
| Node 1    |                        |  LamportClock |
+-----------+                        |               |
                                     +---------------+
                                          |
+-----------+       TCP 8765              |
| Firmware  |----------------------->     |
| Node 2    |                        +---+----------+
+-----------+                        |  Dashboard   |
                                     |  HTTP :8080  |
         UDP Broadcast :9999         |  WS   :8081  |
+-----------+  <-------------------> +---+----------+
| All Nodes |  COOKIE_DISCOVER /         |
| (all 3)   |  COOKIE_SERVER             v
+-----------+                       Browser /
                                    index.html
```

---

## Camadas e Responsabilidades

### Firmware

| Camada | Localização | Responsabilidade | Dependências diretas |
|--------|------------|-----------------|---------------------|
| Ponto de Entrada | `firmware/main.c` | Inicializa hardware, cria tarefas FreeRTOS, inicia scheduler | `shared_state`, `lamport`, `button_handler`, `display`, `ws2812`, `buzzer`, `app_queues` |
| Tarefa RPC | `firmware/tasks/task_rpc.c` | Gerencia WiFi, discovery, ciclo de 20 ms, circuit breaker, heartbeat | `rpc_client`, `service_disc`, `shared_state`, `lamport`, `app_queues` |
| Cliente RPC | `firmware/rpc_client.c` | Acesso à rede: encapsula TCP, JSON build e parse. Nenhum outro arquivo usa lwIP diretamente | `lwip/sockets`, `FreeRTOS` |
| Tarefa Botões | `firmware/tasks/task_buttons.c` | Consome `pending_clicks` do `shared_state` e recoloca (feedback + roteamento) | `shared_state`, `app_queues`, `ws2812`, `game_events` |
| Tarefa Display | `firmware/tasks/task_display.c` | Renderiza OLED e matriz WS2812; lê snapshot atômico | `shared_state`, `display`, `ws2812` |
| Tarefa Monitor | `firmware/tasks/task_monitor.c` | Telemetria periódica (5 s) via UART | `shared_state`, `rpc_client`, `lamport`, `app_queues` |
| Estado Compartilhado | `firmware/middleware/shared_state.c/.h` | Região de memória protegida por spinlock de hardware | `hardware/sync` (Pico SDK) |
| Relógio Lamport | `firmware/middleware/lamport.c/.h` | Relógio lógico incremental; usa o mesmo spinlock do `shared_state` | `shared_state` |
| Filas | `firmware/middleware/app_queues.c/.h` | Inicializa e expõe `QueueHandle_t` da fila de cliques | `FreeRTOS/queue` |
| Button Handler | `firmware/middleware/button_handler.c/.h` | Configura ISR dos botões; incrementa `pending_clicks` atomicamente | `shared_state` |
| Discovery | `firmware/discovery/service_disc.c/.h` | UDP broadcast + parse da resposta via lwIP UDP | `lwip/udp` |
| Display Driver | `firmware/drivers/display/display.c`, `ssd1306_i2c.c` | Abstrai SSD1306 via I2C | `hardware_i2c` (Pico SDK) |
| LED Matrix | `firmware/drivers/display/ws2812.c` | Controla matriz WS2812 via PIO | `hardware_pio` |
| Buzzer | `firmware/audio/buzzer.c` | Gera tons via PWM | `hardware_pwm` |

### Servidor

| Camada | Localização | Responsabilidade | Dependências diretas |
|--------|------------|-----------------|---------------------|
| Ponto de Entrada | `server/app/main.py` | Inicializa todos os serviços, injeta dependências, chama `serve_forever` | `asyncio`, todas as camadas abaixo |
| Configuração | `server/app/config.py` | Centraliza constantes de porta e paths; lê variáveis de ambiente | `os`, `pathlib` |
| Dispatcher RPC | `server/infra/rpc_server.py` | Aceita conexões TCP, parseia JSON-RPC 2.0, roteia para handlers, responde | `asyncio`, `GameManager`, `NodeRegistry`, `GameRepository` |
| Gerente de Jogo | `server/domain/game_manager.py` | Rate limiting, score, milestones, powerup, sync_offline | `NodeRegistry`, `GameRepository`, `LamportClock` |
| Registro de Nós | `server/domain/node_registry.py` | Register, heartbeat, mark_inactive (60 s), get_active_nodes | `db`, `LamportClock` |
| Relógio Lamport | `server/domain/lamport_clock.py` | Mantém `last_lamport` por nó; violações detectadas no GameManager | (nenhuma) |
| Repositório de Jogo | `server/domain/game_repository.py` | Fachada para operações DB relacionadas ao jogo | `infra/db` |
| Banco de Dados | `server/infra/db.py` | CRUD assíncrono sobre SQLite via `aiosqlite` | `aiosqlite` |
| Logger | `server/infra/logger.py` | Log estruturado JSON por nível (ERROR/NORMAL/VERBOSE/TRACE) | `os`, `json` |
| Discovery UDP | `server/infra/udp_discovery.py` | Escuta UDP :9999, responde `COOKIE_SERVER:<porta>` | `asyncio.DatagramProtocol` |
| Dashboard HTTP | `server/dashboard/http.py` | Servidor HTTP manual; serve `/`, `/api/violations`, `/static/` | `asyncio` |
| Dashboard WS | `server/dashboard/websocket.py` | WebSocket broadcast de estado; notifica eventos de domínio | `websockets` |
| State Service | `server/dashboard/state_service.py` | Coleta snapshot do estado para o dashboard | `db`, `GameManager`, `NodeRegistry` |

---

## Fluxo de Boot Completo

### Firmware (por nó)

```
1.  main() executa em Core 0
2.  stdio_init_all(); setvbuf(stdout/stderr, _IONBF)
3.  sleep_ms(1500)  [aguarda USB enumerar]
4.  shared_state_init()    -> aloca spinlock de hardware
5.  lamport_init()         -> L = 0 (usa spinlock do shared_state)
6.  button_handler_init()  -> configura ISR dos pinos GPIO 5, 6, 22
7.  display_init()         -> inicia I2C1 (SDA=14, SCL=15), SSD1306
8.  led_matrix_init()      -> inicia PIO para WS2812 (pino 7)
9.  buzzer_init()          -> configura PWM no pino 21
10. app_queues_init()      -> cria QueueHandle_t de cliques (profundidade 64)
11. xTaskCreate(task_buttons, priority=3, stack=512)
12. xTaskCreate(task_rpc,     priority=2, stack=4096)
13. xTaskCreate(task_display, priority=1, stack=1024)
14. xTaskCreate(task_monitor, priority=1, stack=768)
15. vTaskStartScheduler() [scheduler assume o controle]

--- task_rpc começa a executar ---

16. cyw43_arch_init()         -> inicializa chip WiFi CYW43
17. cyw43_arch_enable_sta_mode()
18. cyw43_arch_wifi_connect_timeout_ms(SSID, PASSWORD, WPA2, 15000ms)
    [falha -> shared_state_set_connection_status(STATUS_OFFLINE); loop de retry 30s]
    [sucesso -> shared_state_set_connection_status(STATUS_CONNECTING)]
19. service_disc_discover(timeout=3s)
    -> UDP broadcast "COOKIE_DISCOVER:NODE_ID:<N>" para porta 9999
    -> aguarda resposta "COOKIE_SERVER:<porta>"
    -> [success] rpc_client_set_server(ip_descoberto, porta); fallback_in_use=false
    -> [timeout] rpc_client_set_server_fallback()           ; fallback_in_use=true
20. rpc_init()  -> inicializa estrutura interna RpcState (sock=-1)
21. rpc_register_node(NODE_ID)   [com retry exponencial 1->2->5->10s]
    -> TCP connect ao servidor
    -> envia JSON-RPC: register_node
    -> [falha] consecutive_register_failures++; STATUS_OFFLINE; retry com backoff
    -> [sucesso] registered=true
22. Drena fila de cliques para pending_clicks (xQueueReceive com ticks=0)
23. Se pending_clicks > 0:
    -> shared_state_set_syncing_count(total)
    -> shared_state_set_connection_status(STATUS_SYNCING)
    -> lamport_tick(); rpc_sync_offline(total, lamport_ts)
    -> lamport_update(resp.lamport_ts)
    -> shared_state_set_scores(&resp)
24. shared_state_set_connection_status(STATUS_ONLINE)
25. Servidor registra no como ACTIVE (register_node chama update_node no DB)
```

### Servidor

```
1.  asyncio.run(main())
2.  await db.init_db()         -> CREATE TABLE IF NOT EXISTS nodes/events/milestones/lamport_violations
3.  clock = LamportClock()
4.  registry = NodeRegistry(clock)
5.  game_repo = GameRepository()
6.  game_lock = asyncio.Lock()
7.  manager = GameManager(registry, game_repo, clock, lock=game_lock)
8.  await manager.reconstruct_global_score()
    -> SELECT SUM(local_score) FROM nodes -> reconstrói global_score em memória
9.  rpc_server, get_active_conns = await start_rpc_server(manager, registry, game_repo,
        host="0.0.0.0", port=8765)
    -> asyncio.start_server() registra handle_client como corrotina de conexão
10. await start_udp_discovery(loop, rpc_port=8765, discovery_port=9999)
    -> loop.create_datagram_endpoint() com allow_broadcast=True
11. _broadcast_task, ws_server = await start_dashboard(...)
    -> DashboardHTTPServer.start()    -> asyncio.start_server(:8080)
    -> DashboardWebSocketServer.start() -> ws_serve(:8081)
    -> asyncio.create_task(ws_server.broadcast_loop())  [broadcast a cada 5s]
    -> manager.set_notifier(ws_server.broadcast)
    -> registry.set_notifier(ws_server.broadcast)
12. asyncio.create_task(background_tasks(registry))
    -> loop: await registry.mark_inactive(); await asyncio.sleep(10)
13. start_debug_server(port=8090) [endpoint /debug]
14. async with rpc_server: await rpc_server.serve_forever()
    [servidor pronto — handle_client() é invocado para cada conexão TCP]
```

---

## Fluxo de uma Requisição RPC: `add_clicks`

```
Firmware (task_rpc.c):
  1. cycle_start = time_us_64()
  2. pending_clicks = shared_state_take_pending_clicks()   [spinlock; zera contador]
  3. lamport_sent = lamport_tick()                          [L = L + 1; retorna L]
  4. rpc_add_clicks(pending_clicks, lamport_sent)
       -> build_add_clicks_json():
          {"jsonrpc":"2.0","method":"add_clicks",
           "params":{"node_id":<N>,"clicks":<C>,"lamport_ts":<L>},"id":1}\n
       -> rpc_call_with_retry() [ate 3 tentativas, backoff 100/200/400ms]
           -> rpc_call_once()
               -> connect_to_server()  [socket TCP persistente; reconecta se fechado]
               -> send_all()           [lwip_send() ate esgotar buffer]
               -> select(RECV_TIMEOUT_MS=2000ms) + lwip_recv()

Rede TCP:

Servidor (rpc_server.py - handle_client):
  5. line = await reader.readline()   [corrotina suspende ate dados]
  6. dispatcher.dispatch(request_str, client_ip)
  7. req = json.loads(request_json)   [valida jsonrpc=="2.0"]
  8. method = "add_clicks"; handler = game_manager.add_clicks
  9. result = await handler(**params)   [params={node_id, clicks, lamport_ts}]

Servidor (game_manager.py - _add_clicks_logic):
  10. async with game_lock:
  11. last_ts = await lamport_clock.get_last_by_node(node_id)
  12. if lamport_ts <= last_ts:           [VIOLACAO LAMPORT]
          curr = await lamport_clock.update(node_id, lamport_ts)
          await game_repo.insert_lamport_violation(...)
          return {"error": "LAMPORT_VIOLATION", "lamport_ts": curr}
  13. delta = now - node["last_ts"];  node["last_ts"] = now
  14. allowed = int(RATE_LIMIT * delta)  [RATE_LIMIT = 50 clicks/s]
      if delta > 3600: accepted = clicks  [excecao: ausencia > 1h]
      else: accepted = min(clicks, allowed)
  15. rejected = clicks - accepted
  16. actual_clicks = accepted * node["multiplier"]  [multiplier=3 se powerup ativo]
  17. node["score"] += actual_clicks; global_score += actual_clicks
  18. await game_repo.update_score(node_id, node["score"])
  19. lamport_ts = await lamport_clock.update(node_id, lamport_ts)
      [L_server = max(L_server, lamport_ts) + 1]
  20. Detecta milestones [100,500,1000,5000,10000]; insert_milestone se atingido
  21. await game_repo.insert_event(node_id, clicks, accepted, rate_exceeded, lamport_ts)
  22. await node_registry.heartbeat(node_id)   [atualiza last_seen]
  23. await self._notify("click_batch", {...})  [broadcast WebSocket]
  24. return {"status":..., "accepted_clicks":..., "global_score":...,
              "node_score":..., "lamport_ts":..., "milestone":..., ...}

Servidor (rpc_server.py):
  25. response_str = json.dumps({"jsonrpc":"2.0","result":result,"id":1}) + "\n"
  26. writer.write(response_str.encode()); await writer.drain()

Firmware (rpc_client.c - parse_add_clicks_response):
  27. json_get_int(json, "global_score", &result.global_score)
      json_get_int(json, "node_score",  &result.local_score)
      json_get_int(json, "lamport_ts",  &temp_lamport)
      json_contains(json, "\"milestone\":true")

Firmware (task_rpc.c):
  28. lamport_update(click_res.lamport_ts)  [L = max(L,recv_ts)+1]
  29. shared_state_set_scores(&click_res)    [spinlock; atualiza local+global+status]
  30. elapsed = time_us_64() - cycle_start
      sleep_us = max(0, CYCLE_PERIOD_US - elapsed)  [CYCLE_PERIOD_US = 20000]
      vTaskDelay(pdMS_TO_TICKS(sleep_us / 1000))
```

---

## Fluxo de Falha e Recuperação (Circuit Breaker)

```
Estado CLOSED (online):
  - consecutive_rpc_failures == 0
  - registered == true
  - connection_status == STATUS_ONLINE

Condicao de abertura do circuito:
  - Cada falha de rede (RPC_TIMEOUT ou RPC_DISCONNECTED) em:
      * rpc_add_clicks() ou
      * rpc_register_node() no heartbeat (a cada 30s)
    incrementa consecutive_rpc_failures++
  - Violacoes de Lamport e Rate Limit nao contam para o circuito
  - Quando consecutive_rpc_failures >= 3:
      -> shared_state_set_connection_status(STATUS_OFFLINE)
      -> consecutive_rpc_failures = 0
      -> registered = false  [circuito ABERTO]

Estado OPEN (offline):
  - Cliques de shared_state_take_pending_clicks() sao devolvidos via
    shared_state_restore_clicks()  [acumulam em pending_clicks]
  - task_display exibe "!! OFFLINE"
  - task_buttons continua registrando cliques via ISR -> shared_state_increment_pending_clicks()
  - registered == false => task_rpc entra no bloco "!registered"

Tentativas de reconexao (HALF-OPEN):
  - Backoff exponencial controlado por consecutive_register_failures:
      falha 0: prox tentativa em 1000ms  (RPC_REGISTER_BACKOFF_1)
      falha 1: prox tentativa em 2000ms  (RPC_REGISTER_BACKOFF_2)
      falha 2: prox tentativa em 5000ms  (RPC_REGISTER_BACKOFF_3)
      falha 3+: prox tentativa em 10000ms (RPC_REGISTER_BACKOFF_MAX)
  - rpc_register_node() tenta TCP + JSON-RPC
  - [falha] consecutive_register_failures++; STATUS_OFFLINE; aguarda backoff
  - [sucesso] -> drain da fila -> sync_offline (se pending_clicks > 0) -> STATUS_ONLINE
               -> consecutive_rpc_failures = 0; registered = true  [circuito FECHADO]

Sincronizacao offline ao fechar o circuito:
  1. Drain de toda a fila de cliques para pending_clicks
  2. total_to_sync = shared_state_take_pending_clicks()
  3. shared_state_set_connection_status(STATUS_SYNCING)
  4. lamport_tick(); rpc_sync_offline(total_to_sync, lamport_ts)
  5. [sucesso] lamport_update(); shared_state_set_scores(); STATUS_ONLINE
  6. [falha] shared_state_restore_clicks(total_to_sync); registered=false; STATUS_OFFLINE

Heartbeat complementar (deteccao de falha pelo servidor):
  - O servidor marca nos como INACTIVE apos 60s sem heartbeat (mark_inactive(), rodado a cada 10s)
  - O firmware envia rpc_register_node a cada 30s (HEARTBEAT_INTERVAL_US=30000000)
    enquanto STATUS_ONLINE; falhas contam para consecutive_rpc_failures
```

---

## Inconsistencias Encontradas

1. **`README.md` menciona "Core 1"** (linha 26: "Core 0: Inicialização... loop principal") mas o firmware usa FreeRTOS no Core 0. Nao ha chamadas a `pico_multicore` ou `multicore_launch_core1` no codigo. A biblioteca `pico_multicore` consta no `CMakeLists.txt` mas nao e usada.

2. **`test_rpc.py` passa `node_id` como string** (`f"node_{client_id}"`) mas `register_node` no `NodeRegistry` e `GameManager` usa inteiros. A linha `assert f"node_{client_id}" in resp.get("result", [])` provavelmente falha em runtime.

3. **`rpc_client.c` (funcao `parse_activate_powerup_response`)**: quando o servidor retorna `ALREADY_ACTIVE`, o firmware fixa `powerup_remaining_s = 10` em vez de parsear do JSON. O servidor nao inclui `time_remaining` nessa resposta, entao o hardcode e necessario, mas fica divergente do que o README descreve.
