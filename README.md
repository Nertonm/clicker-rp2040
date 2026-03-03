# clicker-rp2040

Sistema distribuído de cliques em tempo real para a BitDogLab (RP2040).
Múltiplas placas competem por um placar global via JSON-RPC sobre TCP, com
sincronização causal e reconexão automática.

## O que faz

Cada placa registra pressionamentos do botão A e os envia periodicamente
a um servidor. O servidor consolida os scores, valida a ordem causal dos
eventos e notifica um dashboard web em tempo real.

Se uma placa perder conexão, ela acumula os cliques localmente e os
sincroniza quando o servidor voltar, sem perder nenhum evento.

---

[BitDogLab #0] ->
[BitDogLab #1] -> Servidor (TCP 8765)
[BitDogLab #2] ->       |
                Camadas: App -> Domain -> Infra (SQLite)
                        |
                 Dashboard Web (HTTP 8080 / WS 8081)

**Firmware (C / Pico SDK)**
- Core 0: Inicialização, WiFi, leitura de botões e loop principal.
- Estado compartilhado protegido por spinlock via `shared_state`.

**Servidor  stack a definir**
- Dispatcher JSON-RPC 2.0 com suporte a múltiplas conexões simultâneas
- Lógica de jogo: rate limiting, milestones, power-up
- Rastreamento de nós ativos e heartbeats
- Camada de persistência isolada (async)
- Dashboard com WebSocket em tempo real

**Protocolos**
- Discovery: broadcast UDP 9999 com `COOKIE_DISCOVER` / `COOKIE_SERVER`
- RPC: JSON-RPC 2.0 sobre TCP 8765
- Dashboard: HTTP 8080 e WebSocket 8081

---

## Hardware necessário

- 3× BitDogLab (RP2040 + CYW43 WiFi)
- Display OLED SSD1306 via I2C (endereço 0x3C)
- Matriz WS2812 5×5 via PIO
- Buzzer passivo via PWM
- Rede WiFi 2.4 GHz compartilhada entre placas e servidor

---

## Build e Gravação das Placas

### Pré-requisitos

| Dependência | Verificação |
|-------------|-------------|
| `PICO_SDK_PATH` definido no ambiente | `echo $PICO_SDK_PATH` |
| `cmake` ≥ 3.13 | `cmake --version` |
| `arm-none-eabi-gcc` | `arm-none-eabi-gcc --version` |

### Build via script (recomendado)

```bash
cd clicker-rp2040
WIFI_SSID="MinhaRede" \
WIFI_PASSWORD="senha123" \
FALLBACK_SERVER_IP="192.168.0.10" \
./firmware/build_all.sh
```

O script cria `firmware/dist/` com os três binários e imprime o SHA256 de cada um.

### Build manual (target individual)

```bash
cd firmware
mkdir -p build && cd build
cmake .. -DPICO_BOARD=pico_w \
         -DWIFI_SSID="MinhaRede" \
         -DWIFI_PASSWORD="senha123" \
         -DFALLBACK_SERVER_IP="192.168.0.10"

# Compila tudo:
cmake --build . --target all_nodes

# Ou apenas um nó (útil durante apresentação):
cmake --build . --target node1   # → avocado_node1.uf2
```

### Build local com secrets.h

Copie o template e preencha suas credenciais — **nunca commite este arquivo**:

```bash
cp firmware/config/secrets.h.template firmware/config/secrets.h
# edite firmware/config/secrets.h com WIFI_SSID, WIFI_PASSWORD, FALLBACK_SERVER_IP
cmake ..   # sem -D; credenciais vêm de secrets.h
cmake --build . --target all_nodes
```

### Mapeamento placa → arquivo

| Arquivo `.uf2` | Placa | Identificação |
|----------------|-------|---------------|
| `avocado_node0.uf2` | Placa 0 | Etiqueta NODE-0 ou número de série [preencher] |
| `avocado_node1.uf2` | Placa 1 | Etiqueta NODE-1 ou número de série [preencher] |
| `avocado_node2.uf2` | Placa 2 | Etiqueta NODE-2 ou número de série [preencher] |

### Gravação

1. Segure **BOOTSEL** ao conectar a placa via USB — ela aparece como drive `RPI-RP2`
2. Copie o `.uf2` correspondente para o drive
3. A placa reinicia automaticamente com o firmware gravado

```bash
# Exemplo Linux (ajuste /media/$USER/RPI-RP2):
cp firmware/dist/avocado_node0.uf2 /media/$USER/RPI-RP2/
```


---

## Painel administrativo e Dashboard

O servidor expõe um dashboard místico em tempo real. Para detalhes de como subir e configurar, consulte o [Guia do Dashboard](docs/dashboard_guide.md).

---

## Fluxo de uma sessão

1. Ligue as três placas. Elas descobrem o servidor via UDP e se registram.
2. Pressione o botão A para acumular cliques. O Core 0 os envia a cada 20 ms.
3. Pressione o botão B para ativar o multiplicador ×3 por 10 segundos.
4. Desligue o servidor  as placas entram em OFFLINE e continuam acumulando.
5. Religue o servidor  cada placa sincroniza os cliques pendentes via `sync_offline`.
6. O dashboard reflete tudo em menos de 1 segundo via WebSocket.

---

server/
  app/          ponto de entrada (main.py) e config
  domain/       lógica de jogo (GameManager, NodeRegistry)
  infra/        persistência (db.py) e servidores (rpc)
  dashboard/    sistema de visualização modular
  
firmware/
  main.c                fluxo principal e loop Multicore
  rpc_client.c/h        API pública de rede (único ponto de lwIP)
  
  **Nova Interface RPC (v2.0)**
  - `rpc_client.h` expõe 9 funções (6 operacionais + 3 utilitárias)
  - Structs especializados por operação (evita desperdício RAM)
  - Reconexão automática com fila offline
  - Enum `RpcError` para tratamento semântico de erros
  - Nenhum outro arquivo conhece lwIP ou sockets
  
  middleware/
    lamport.c/h         relógio de Lamport thread-safe
    shared_state.c/h    estado compartilhado entre rotinas (spinlock)
  drivers/              em desenvolvimento (OLED, LED Matrix)
  audio/                em desenvolvimento (Buzzer)
  net/                  em desenvolvimento (RPC, Discovery)
  
docs/                   documentação técnica
  - [Arquitetura RPC](docs/rpc_client_api.md)
  - [Setup FreeRTOS](docs/freertos_setup.md)
```

---

## Relógio de Lamport

O timestamp lógico é incrementado antes de cada envio (`lamport_tick`)
e atualizado com o valor do servidor após cada resposta bem-sucedida
(`lamport_update = max(local, server) + 1`). O servidor rejeita eventos
com `lamport_ts <= last_accepted[node_id]` com erro `LAMPORT_VIOLATION`.

Isso garante que o servidor consiga detectar pacotes atrasados ou
repetidos e rejeitá-los, mantendo o log de eventos em ordem causal
verificável.

---

## Identificação das placas

| Arquivo `.uf2`        | NODE_ID | Cor LED |
|-----------------------|---------|---------|
| avocado_node0.uf2     | 0       | Verde   |
| avocado_node1.uf2     | 1       | Azul    |
| avocado_node2.uf2     | 2       | Amarelo |

Use etiquetas físicas para identificar as placas durante testes.

