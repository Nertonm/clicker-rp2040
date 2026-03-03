# OPERATIONS.md - Guia de Operacao

---

## Dependencias e Versoes

### Servidor Python

| Dependencia | Importada por | Uso |
|------------|--------------|-----|
| `aiosqlite` | `server/infra/db.py` | Acesso assincrono ao SQLite |
| `websockets` | `server/dashboard/websocket.py` | Servidor WebSocket (importa `websockets.asyncio.server`) |
| `asyncio` | todos os modulos server | Event loop (stdlib Python 3.10+) |
| `json` | `rpc_server.py`, `websocket.py`, `logger.py` | Serializacao JSON (stdlib) |
| `pathlib` | `config.py` | Caminhos de arquivo (stdlib) |
| `threading` | `logger.py` | Lock para metricas thread-safe (stdlib) |

Versao minima de Python inferida pela sintaxe `asyncio.start_server` com `async with`: **Python 3.10+**. A importacao `from websockets.asyncio.server import serve` requer **websockets >= 12.0**.

Arquivo `requirements.txt` nao encontrado no repositorio. As dependencias devem ser instaladas manualmente:

```bash
pip install aiosqlite websockets
```

### Firmware

| Dependencia | Versao no CMakeLists.txt | Papel |
|------------|------------------------|-------|
| Raspberry Pi Pico SDK | 2.2.0 | Base de hardware, lwIP, WiFi (CYW43), PIO |
| FreeRTOS Kernel | repo em `third_party/FreeRTOS-Kernel` | Scheduler, tarefas, filas |
| arm-none-eabi-gcc | 14.2 (toolchain slot `14_2_Rel1`) | Compilador ARM |
| cmake | >= 3.13 | Build system |
| picotool | 2.2.0-a4 | Geracao de UF2 |

---

## Variaveis de Ambiente do Servidor

Lidas em `server/app/config.py` e `server/infra/logger.py`:

| Variavel | Default | Obrig. | Descricao | Impacto se ausente |
|---------|---------|--------|-----------|-------------------|
| `DEBUG_HTTP_PORT` | `8090` | Nao | Porta do servidor HTTP de diagnostico (`/debug`) | Servidor sobe na porta 8090 |
| `SIMULATE_PROCESSING_DELAY_MS` | `0` | Nao | Delay artificial por chamada RPC (ms) | Nenhum delay; concorrencia e menos visivel |
| `LOG_LEVEL` | `NORMAL` | Nao | Verbosidade dos logs: `ERROR`, `NORMAL`, `VERBOSE`, `TRACE` | Log no nivel NORMAL |
| `DB_SLOW_MS` | `50` | Nao | Limiar (ms) para alertar queries lentas do SQLite | Threshold de 50ms |
| `RPC_STATS_EVERY` | `100` | Nao | A cada N dispatches, emite dump de metricas no log | Dump a cada 100 chamadas |

Portas hardcoded em `server/app/config.py` (nao sao variaveis de ambiente):

| Parametro | Valor | Descricao |
|----------|-------|-----------|
| `RPC_HOST` | `"0.0.0.0"` | Interface de escuta do servidor RPC |
| `RPC_PORT` | `8765` | Porta TCP do servidor RPC |
| `UDP_DISCOVER_PORT` | `9999` | Porta UDP de discovery |
| `DASHBOARD_HTTP_PORT` | `8080` | Porta HTTP do dashboard |
| `DASHBOARD_WS_PORT` | `8081` | Porta WebSocket do dashboard |
| `DB_FILE` | `server/avocado.db` | Caminho do banco SQLite |

---

## Procedimento de Build do Firmware

### Pre-requisitos

```bash
# Verifica SDK configurado
echo $PICO_SDK_PATH   # deve apontar para pico-sdk/pico-sdk/

# Verifica compilador
arm-none-eabi-gcc --version

# Verifica cmake
cmake --version       # deve ser >= 3.13
```

### Build via script (todos os 3 nos)

```bash
cd clicker-rp2040
WIFI_SSID="MinhaRede" \
WIFI_PASSWORD="SenhaWiFi" \
FALLBACK_SERVER_IP="192.168.0.10" \
./firmware/build_all.sh
```

O script gera os binarios em `firmware/dist/`:
- `avocado_node0.uf2` (NODE_ID=0)
- `avocado_node1.uf2` (NODE_ID=1)
- `avocado_node2.uf2` (NODE_ID=2)

### O que cada variavel configura no binario

| Variavel de ambiente | Macro C resultante | Onde usada |
|---------------------|-------------------|------------|
| `WIFI_SSID` | `WIFI_SSID` | `net_config.h` / `task_rpc.c` - passa para `cyw43_arch_wifi_connect_timeout_ms` |
| `WIFI_PASSWORD` | `WIFI_PASSWORD` | `net_config.h` / `task_rpc.c` - idem |
| `FALLBACK_SERVER_IP` | `FALLBACK_SERVER_IP` | `rpc_client.c` - IP de fallback se UDP discovery falhar |

### Build manual (target individual)

```bash
cd firmware
mkdir -p build && cd build
cmake .. \
  -DPICO_BOARD=pico_w \
  -DWIFI_SSID="MinhaRede" \
  -DWIFI_PASSWORD="SenhaWiFi" \
  -DFALLBACK_SERVER_IP="192.168.0.10"

# Todos os 3 nos:
cmake --build . --target all_nodes

# Apenas um no:
cmake --build . --target node1   # gera avocado_node1.uf2
```

### Build local com secrets.h

```bash
cp firmware/config/secrets.h.template firmware/config/secrets.h
# edite secrets.h com WIFI_SSID, WIFI_PASSWORD e FALLBACK_SERVER_IP
# nunca commite secrets.h

cd firmware && mkdir -p build && cd build
cmake ..  # sem -D; credenciais vem de secrets.h
cmake --build . --target all_nodes
```

### Verificar NODE_ID gravado no binario

```bash
# Verifica que o node_id correto foi embutido (string "NODE_ID=" nao existe no binario,
# mas a macro e usada como literal inteiro na main.c):
strings firmware/dist/avocado_node1.uf2 | grep "NODE_ID=1"
# Alternativa: verificar a mensagem de boot no log serial:
# "[INIT] FreeRTOS: x.xx.x  NODE_ID=1  DEBUG_LEVEL=..."
```

---

## Procedimento de Gravacao

### Identificacao da placa fisica

| Arquivo `.uf2` | NODE_ID | Como identificar |
|----------------|---------|----------------|
| `avocado_node0.uf2` | 0 | Etiqueta "NODE-0" fixada fisicamente na placa |
| `avocado_node1.uf2` | 1 | Etiqueta "NODE-1" |
| `avocado_node2.uf2` | 2 | Etiqueta "NODE-2" |

**Gravar o arquivo errado na placa errada faz o sistema rodar com um NODE_ID incorreto: os scores e heartbeats vao para o no errado.**

### Gravacao no Linux

1. Segure o botao **BOOTSEL** na placa.
2. Conecte o cabo USB enquanto segura BOOTSEL.
3. Solte BOOTSEL. A placa aparece como dispositivo de armazenamento `RPI-RP2`.
4. Copie o `.uf2` correspondente:

```bash
# Ajuste /media/$USER/RPI-RP2 conforme o ponto de montagem real
cp firmware/dist/avocado_node0.uf2 /media/$USER/RPI-RP2/
```

5. A placa reinicia automaticamente e o firmware comeca a executar.
6. Abra monitor serial para confirmar boot:

```bash
# Encontra a porta serial correta:
ls /dev/ttyACM*   # ou /dev/ttyUSB*

# Abre monitor (Ctrl+A K para sair):
screen /dev/ttyACM0 115200
```

Log esperado no boot bem-sucedido:
```
[INIT] ===== Clicker RP2040 - Sprint 2 =====
[INIT] FreeRTOS: 10.6.2  NODE_ID=0  DEBUG_LEVEL=1
[INIT] shared_state_init              OK dt=0 ms
...
[INIT] Iniciando scheduler FreeRTOS...
[WIFI] Conectando em MinhaRede...
[WIFI] Conectado
[DISC] Broadcast enviado: COOKIE_DISCOVER:NODE_ID:0. Aguardando UDP...
[DISC] Sucesso! Servidor em 192.168.0.5:8765
[RPC] Conectado ao servidor 192.168.0.5:8765
```

---

## Procedimento de Inicializacao do Servidor

### Comando de inicializacao

```bash
cd clicker-rp2040/server
LOG_LEVEL=NORMAL \
SIMULATE_PROCESSING_DELAY_MS=0 \
python -m app.main
```

Para demonstracao de concorrencia:

```bash
cd server && SIMULATE_PROCESSING_DELAY_MS=500 python -m app.main
```

### O que observar no log para confirmar inicializacao

Cada linha de log e um JSON:

```json
{"ts":"...","tag":"[DB]","msg":"init_db ok","db_file":"/.../avocado.db","duration_ms":4.12}
{"ts":"...","tag":"[GAME]","msg":"global_score_reconstruido_do_banco","global_score":0,"nodes":0}
{"ts":"...","tag":"[RPC]","msg":"server_started","host":"0.0.0.0","port":8765}
```

Seguido pela impressao no stdout:
```
==================================================
AbacateOS - SERVIDOR INICIADO
IP LOCAL: 192.168.0.5
==================================================

Servicos:
 - RPC: 0.0.0.0:8765
 - UDP Discovery: port 9999
 - Dashboard: http://192.168.0.5:8080
 - Debug:     http://192.168.0.5:8090/debug
```

Para confirmar que cada servico iniciou corretamente:

| Servico | Linha de log a procurar |
|---------|------------------------|
| Banco de dados | `"msg":"init_db ok"` |
| Servidor RPC | `"msg":"server_started","port":8765` |
| Discovery UDP | Nao aparece no log estruturado. Confirmar quando o primeiro no conectar: `[UDP-Discovery] Recebido:` no stdout |
| Dashboard HTTP | Print direto: `[Dashboard] Servidor HTTP em http://0.0.0.0:8080` |
| Dashboard WS | Print direto: `[Dashboard] Servidor WebSocket em ws://0.0.0.0:8081` |

---

## Diagnostico em Tempo Real

### Endpoints disponíveis

| Endpoint | Porta | Protocolo | Descricao |
|----------|-------|----------|-----------|
| Dashboard principal | 8080 | HTTP GET `/` | Interface web com scores, nos, eventos ultimos 20 |
| Dashboard violations | 8080 | HTTP GET `/api/violations` | Ultimas 100 violacoes de Lamport em JSON |
| WebSocket stream | 8081 | WebSocket | Push de eventos em tempo real (`full_state`, `click_batch`, `milestone`, `node_status_change`, `powerup_activated`, `sync_complete`) |
| Debug HTTP | 8090 | HTTP GET `/debug` | [arquivo `infra/debug_server.py` referenciado em `main.py`, nao lido - arquivo adicional] |

### Log estruturado

Cada linha de log tem o formato:
```json
{"ts":"<ISO8601>","tag":"<TAG>","msg":"<mensagem>","campo1":valor1,...}
```

Tags disponíveis: `[INIT]`, `[RPC]`, `[GAME]`, `[SYNC]`, `[LAMPORT]`, `[NODE]`, `[DB]`, `[DISC]`, `[ERROR]`, `[STATS]`, `[DEBUG]`.

### Rastreando problema: no nao aparece como ACTIVE

Sequencia de linhas de log a procurar (em ordem):

```
1. "[UDP-Discovery] Recebido: 'COOKIE_DISCOVER:NODE_ID:0'" 
   -> Discovery UDP chegou; se ausente, o no nao esta enviando broadcast
      (verificar WiFi, porta 9999 nao bloqueada por firewall)

2. {"tag":"[NODE]","msg":"register_novo","node":0,"ip":"..."}
   -> Primeiro registro do no; se ausente, rpc_register_node falhou
      (verificar porta 8765, firewall, log de erro do no via UART)

3. {"tag":"[NODE]","msg":"register_reativacao","node":0}
   -> No reativado (heartbeat); status vai para ACTIVE

4. {"tag":"[GAME]","msg":"clicks_aceitos","node":0,...}
   -> Cliques sendo processados; no esta ACTIVE e operacional

5. Ausente apos 60s: {"tag":"[NODE]","msg":"INATIVO","node":0}
   -> No perdeu conexao; mark_inactive() detectou ausencia de heartbeat
```

### Rastreando violacoes de Lamport

```bash
# Via HTTP
curl http://localhost:8090/api/violations | python3 -m json.tool

# No log (nivel ERROR, sempre visivel):
# {"tag":"[LAMPORT]","msg":"VIOLAÇÃO causal detectada","node":0,"received_ts":3,"server_ts":5}
```

### Metricas de diagnostico do servidor

A cada `RPC_STATS_EVERY` chamadas (default 100), o log emite:
```json
{"tag":"[STATS]","msg":"snapshot_métricas",
 "rpc_calls_total":250,"rpc_calls_ok":248,"rpc_calls_error":2,
 "lamport_violations":1,"rate_limited_clicks":0,"accepted_clicks":1240,
 "milestones_triggered":2,"db_writes":500,"db_reads":50,"db_slow_queries":0,
 "nodes_registered":3,"nodes_marked_inactive":0}
```

### Metricas de diagnostico do firmware

Emitidas via UART a cada 500 ciclos de task_rpc e a cada 5 ciclos de task_monitor:

```
# RPC (task_rpc.c - rpc_print_diagnostics):
[RPC][STATS] total=250 ok=248 timeouts=1 disconnects=1 parse_ok=243 parse_fail=0 connected=1

# Lamport (lamport.c - lamport_print_diagnostics):
[LAMPORT][STATS] ts=253 ticks=248 updates=248 jumps=0 resets=0

# Monitor geral (task_monitor.c):
[STATS] queue=0 heap=65432 min_heap=62000 pending=0 irq=150/152 lamport=253 status=ONLINE
```
