# FIRMWARE.md - Referência de Firmware

---

## Arquitetura de Tasks FreeRTOS

As quatro tasks sao criadas em `firmware/main.c` antes do `vTaskStartScheduler()`. Todas executam no Core 0 (o projeto nao usa `multicore_launch_core1`).

| Task | Nome FreeRTOS | Core | Stack (words) | Prioridade | Responsabilidade |
|------|--------------|------|--------------|-----------|-----------------|
| `task_buttons` | `"buttons"` | 0 | 512 | 3 (Alta) | Consome `pending_clicks` do `shared_state`; roteia o click de volta ao `shared_state` para consumo pelo `task_rpc` |
| `task_rpc` | `"rpc"` | 0 | 4096 | 2 (Media) | WiFi, discovery, registro, ciclo de 20ms, circuit breaker, heartbeat, sincronizacao offline |
| `task_display` | `"display"` | 0 | 1024 | 1 (Baixa) | Renderiza OLED (8 linhas) e matriz WS2812 a cada 50ms |
| `task_monitor` | `"monitor"` | 0 | 768 | 1 (Baixa) | Telemetria periodica (5s) via UART: heap, filas, Lamport, status |

**Nota sobre `task_buttons`:** A task chama `shared_state_take_pending_clicks()` e logo em seguida `shared_state_restore_clicks()` nos dois ramos (online e offline), devolvendo os clicks ao `shared_state` para que `task_rpc` os consuma no proximo ciclo. O comentario no codigo explica que isso foi feito para garantir que `task_rpc` seja o unico consumidor.

---

## Maquina de Estados de Conexao

Definida em `firmware/middleware/shared_state.h` como `connection_status_t`.

```
                     cyw43_arch_wifi_connect() falhou
                     ou credenciais em branco
+-------------------+---------------------------------------------+
|                   |                                             |
|            [STATUS_CONNECTING]  <----+                         |
|           (setup_wifi sucesso)       | register_node falhou    |
|                   |                  | (backoff exponencial)   |
|    setup_network_target()            |                         |
|    rpc_init()                        |                         |
|                   |  register_node OK                          |
|                   v                                            |
|   pending > 0:  [STATUS_SYNCING]                               |
|   sync_offline    |    sync_offline falhou                     |
|   ok              |    <-----[STATUS_OFFLINE]                  |
|                   |                  ^                         |
|                   v                  | 3 falhas RPC            |
|            [STATUS_ONLINE]  ---------+  consecutivas          |
|            (operacao normal)           ou heartbeat falhou     |
+-------------------+---------------------------------------------+
         ^          |
         |          | 3 falhas RPC
         +----------+ (re-registro com backoff exponencial)
```

| Estado | Valor enum | Indicador no OLED | Indicador nos LEDs |
|--------|-----------|------------------|--------------------|
| `STATUS_CONNECTING` | 0 | `~~ Conectando...` | - |
| `STATUS_ONLINE` | 1 | `[*]` no cabecalho | dígito cinza normal |
| `STATUS_OFFLINE` | 2 | `!! OFFLINE` + `[!]` | LED 12 pisca vermelho |
| `STATUS_SYNCING` | 3 | `> Enviando: N` + `[>]` | - |

---

## Ciclo de 20ms do Loop Online

A `task_rpc` opera em ciclos de 20ms (`CYCLE_PERIOD_US = 20000`) enquanto em STATUS_ONLINE, com cronometro baseado em `time_us_64()`.

```
cycle_start = time_us_64()
|
+-- [1] Verifica shared_state_take_turbo_activation_requested()
|         se true: rpc_activate_powerup() + apply_local_turbo()
|
+-- [2] Bloco de registro (se !registered):
|         calcula backoff, tenta rpc_register_node(), drain de fila,
|         sync_offline se pending > 0, STATUS_ONLINE
|         [nao registrado: vTaskDelay(100ms); continue]
|
+-- [3] pending_clicks = shared_state_take_pending_clicks()
|         se STATUS_OFFLINE: restaura clicks; continua
|         se online: lamport_tick(); rpc_add_clicks(pending, lamport_ts)
|          -> sucesso: lamport_update(); shared_state_set_scores()
|          -> LAMPORT_VIOLATION: lamport_update(); nao conta como falha de rede
|          -> RATE_EXCEEDED: lamport_update() + set_scores() dos aceitos;
|                            restore_clicks(rejeitados); nao conta como falha de rede
|          -> erro de rede: restore_clicks(); consecutive_rpc_failures++
|
+-- [4] Heartbeat check (a cada HEARTBEAT_INTERVAL_US = 30s):
|         se STATUS_ONLINE e tempo decorrido >= 30s:
|           rpc_register_node() como heartbeat
|           sucesso: atualiza last_heartbeat_us
|           falha: consecutive_rpc_failures++
|
+-- [5] Circuit breaker check:
|         se consecutive_rpc_failures >= 3:
|           STATUS_OFFLINE; registered=false; counter=0
|
+-- [6] Scores refresh (a cada SCORES_REFRESH_MS = 2000ms):
|         rpc_get_scores() -> shared_state_set_global_score() + set_node_scores()
|
+-- [7] Compensacao de tempo:
         elapsed = time_us_64() - cycle_start
         se elapsed < 20000: vTaskDelay(pdMS_TO_TICKS((20000-elapsed)/1000))
         se elapsed >= 20000: taskYIELD()  [ciclo excedeu orcamento]
```

**Estatísticas:** A cada 500 iteracoes, `rpc_print_diagnostics()` e `lamport_print_diagnostics()` sao emitidos via UART.

---

## Protocolo de Comunicacao no Firmware (`rpc_client.c`)

O `rpc_client.c` e a fronteira entre firmware e rede.

### Conexao TCP

- Socket lwIP: `lwip_socket(AF_INET, SOCK_STREAM, 0)`
- Connect nao-bloqueante com `select()`: timeout de `CONNECT_TIMEOUT_MS = 3000ms`
- Apos conexao: restaura modo bloqueante
- Configura `SO_RCVTIMEO = 2000ms`, `SO_SNDTIMEO = 2000ms`, `SO_KEEPALIVE = 1`
- Socket reutilizado entre chamadas (conexao persistente); reconecta apenas se `sock < 0`

### Envio

- `send_all()`: loop sobre `lwip_send()` ate todo o buffer ser enviado
- Buffer de requisicao: 256 bytes (maximo de uma requisicao JSON)

### Recepcao

- `select()` com timeout `RECV_TIMEOUT_MS = 2000ms` antes do `lwip_recv()`
- Buffer de resposta: `RPC_BUFFER_SIZE = 1024` bytes

### Retry

- `rpc_call_with_retry()`: ate 3 tentativas
- Backoff entre tentativas: `BACKOFF_MS[] = {100, 200, 400}` ms
- `rpc_call_once()`: se `send_and_receive` falhar, fecha o socket e tenta reconectar uma vez antes de retornar erro

### Parsing JSON

Manual, sem biblioteca externa:

- `json_contains(json, substring)`: `strstr()` para presenca de string
- `json_get_int(json, key, out)`: busca `"key":`, avanca whitespace, `sscanf("%d", ...)`
- Os parsers especializados detectam `"error"`, `"LAMPORT_VIOLATION"`, `"RATE_EXCEEDED"` por presenca de substring

### Mapeamento de structs

| Funcao | Struct de retorno | Campos chave |
|--------|------------------|-------------|
| `rpc_register_node()` | `RpcSimpleResult` | `success`, `error_code` |
| `rpc_add_clicks()` | `RpcClickResult` | `success`, `global_score`, `local_score`, `lamport_ts`, `accepted_clicks`, `milestone_triggered`, `milestone_value`, `powerup_remaining_s`, `error_code` |
| `rpc_sync_offline()` | `RpcClickResult` | idem (usa `parse_add_clicks_response`) |
| `rpc_activate_powerup()` | `RpcPowerupResult` | `success`, `powerup_remaining_s`, `error_code` |
| `rpc_get_scores()` | `RpcScoreResult` | `success`, `global_score`, `node_scores[3]`, `error_code` |

---

## Configuracoes em Compile-Time

### `firmware/config/firmware_config.h`

| Macro | Valor default | Unidade | Descricao |
|-------|--------------|---------|-----------|
| `CLICK_QUEUE_LEN` | 64 | itens | Profundidade maxima da fila de cliques FreeRTOS |
| `DISPLAY_PERIOD_MS` | 50 | ms | Intervalo de atualizacao do display e LEDs |
| `MONITOR_PERIOD_MS` | 5000 | ms | Intervalo de telemetria da task_monitor |
| `RPC_POLL_PERIOD_MS` | 100 | ms | Latencia de resposta da task_rpc (modo offline) |
| `CYCLE_PERIOD_US` | 20000 | µs | Periodo exato do ciclo online da task_rpc |
| `SCORES_REFRESH_MS` | 2000 | ms | Intervalo de busca de scores globais |
| `HEARTBEAT_INTERVAL_US` | 30000000 | µs | Intervalo de heartbeat ao servidor (30s) |
| `RPC_REGISTER_BACKOFF_1` | 1000 | ms | Backoff apos 1a falha de registro |
| `RPC_REGISTER_BACKOFF_2` | 2000 | ms | Backoff apos 2a falha de registro |
| `RPC_REGISTER_BACKOFF_3` | 5000 | ms | Backoff apos 3a falha de registro |
| `RPC_REGISTER_BACKOFF_MAX` | 10000 | ms | Backoff maximo (4a+ falha) |
| `WIFI_RETRY_INTERVAL_MS` | 30000 | ms | Intervalo de retry de conexao WiFi |
| `TURBO_DURATION_MS` | 10000 | ms | Duracao do efeito power-up (turbo) |
| `MILESTONE_GLOW_TICKS` | 10 | ciclos de display | Duracao do efeito visual de milestone |

### `firmware/hardware_config.h` (localizado na raiz de `firmware/`)

| Macro | Valor | Descricao |
|-------|-------|-----------|
| `LED_RED` | 13 | GPIO do LED vermelho |
| `LED_GREEN` | 11 | GPIO do LED verde |
| `LED_BLUE` | 12 | GPIO do LED azul |
| `BUTTON1_PIN` | 5 | GPIO do Botao A (Pressionado = LOW) |
| `BUTTON2_PIN` | 6 | GPIO do Botao B (Pressionado = LOW) |
| `BUTTONSTICK_PIN` | 22 | GPIO do botao do joystick |
| `WS2812_PIN` | 7 | GPIO de dados da matriz NeoPixel |
| `WS2812_NUM_LEDS` | 25 | Total de LEDs na matriz 5x5 |
| `WS2812_IS_RGBW` | false | LEDs sem canal branco |
| `BUZZER_A_PIN` | 21 | GPIO do buzzer passivo (PWM) |
| `I2C_PORT` | i2c1 | Instancia I2C do display |
| `I2C_SDA` | 14 | Pino SDA |
| `I2C_SCL` | 15 | Pino SCL |
| `OLED_I2C_ADDR` | 0x3C | Endereco I2C do SSD1306 |
| `OLED_I2C_FREQ` | 400000 | Hz | Frequencia I2C (400kHz) |
| `JOY_VRX_PIN` | 27 | GPIO eixo X joystick (ADC1) |
| `JOY_VRY_PIN` | 26 | GPIO eixo Y joystick (ADC0) |
| `JOY_SW_PIN` | 22 | GPIO botao do joystick (identico a BUTTONSTICK_PIN) |
| `MIC_PIN` | 28 | GPIO microfone analogico (ADC2) |
| `NODE_ID` | 0 | ID do no (sobrescrito via `-DNODE_ID=N` em compile time) |

### `firmware/config/net_config.h`

| Macro | Valor default | Descricao |
|-------|--------------|-----------|
| `WIFI_SSID` | `""` | SSID da rede WiFi (sobrescrito via env ou secrets.h) |
| `WIFI_PASSWORD` | `""` | Senha WiFi (sobrescrito via env ou secrets.h) |

`FALLBACK_SERVER_IP` nao e definido aqui - vive em `rpc_client.c` com guard `#ifndef`.

### Macros definidas em `CMakeLists.txt` (por target)

| Macro | Valor | Descricao |
|-------|-------|-----------|
| `DISPLAY_SSD1306` | 1 | Ativa compilacao condicional do driver SSD1306 |
| `PICO_HEAP_SIZE` | 0x14000 (80 KB) | Tamanho do heap FreeRTOS |
| `configTICK_RATE_HZ` | 1000 | Frequencia do tick do scheduler (1ms por tick) |
| `NODE_ID` | 0, 1 ou 2 | Identificador do no, diferente por target (node0/node1/node2) |
| `WIFI_SSID` | valor do env | Injetado via `-DWIFI_SSID=...` |
| `WIFI_PASSWORD` | valor do env | Injetado via `-DWIFI_PASSWORD=...` |
| `FALLBACK_SERVER_IP` | valor do env | Sobrescreve o default `192.168.0.10` em `rpc_client.c` |

### `rpc_client.c` (hardcoded, sem header)

| Macro | Valor | Unidade | Descricao |
|-------|-------|---------|-----------|
| `FALLBACK_SERVER_IP` | `"192.168.0.10"` | - | IP padrao se discovery falhar e nao for sobrescrito |
| `FALLBACK_SERVER_PORT` | 8765 | - | Porta TCP do servidor RPC |
| `RECV_TIMEOUT_MS` | 2000 | ms | Timeout de recepcao via socket |
| `SEND_TIMEOUT_MS` | 2000 | ms | Timeout de envio via socket |
| `CONNECT_TIMEOUT_MS` | 3000 | ms | Timeout de conexao TCP |
| `RPC_BUFFER_SIZE` | 1024 | bytes | Buffer de resposta RPC |
