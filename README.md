# clicker-rp2040

Sistema distribuído de cliques em tempo real para a BitDogLab (RP2040).
Três placas competem por um placar global via JSON-RPC sobre TCP, com
sincronização causal por relógio de Lamport e reconexão automática.

## O que faz

Cada placa registra pressionamentos do botão A e os envia periodicamente
a um servidor. O servidor consolida os scores, valida a ordem causal dos
eventos e notifica um dashboard web em tempo real.

Se uma placa perder conexão, ela acumula os cliques localmente e os
sincroniza quando o servidor voltar, sem perder nenhum evento.

---

## Arquitetura

```
[BitDogLab #0] ->
[BitDogLab #1] -> servidor (JSON-RPC 2.0 sobre TCP)
[BitDogLab #2] ->               |            
                         banco de dados
                     Dashboard WebSocket -> browser
```

**Firmware (C / Pico SDK)**
- Core 0: loop de 20 ms — envia cliques via RPC, gerencia WiFi e reconexão
- Core 1: loop de 100 ms — atualiza OLED, LEDs WS2812 e fila de buzzer
- Estado compartilhado protegido por spinlock

**Servidor — stack a definir**
- Dispatcher JSON-RPC 2.0 com suporte a múltiplas conexões simultâneas
- Lógica de jogo: rate limiting, milestones, power-up
- Rastreamento de nós ativos e heartbeats
- Camada de persistência isolada (async)
- Dashboard com WebSocket em tempo real

**Protocolos**
- Discovery: broadcast UDP 9999 com `COOKIE_DISCOVER` / `COOKIE_SERVER`
- RPC: JSON-RPC 2.0 sobre TCP 8765
- Dashboard: WebSocket 8080

---

## Hardware necessário

- 3× BitDogLab (RP2040 + CYW43 WiFi)
- Display OLED SSD1306 via I2C (endereço 0x3C)
- Matriz WS2812 5×5 via PIO
- Buzzer passivo via PWM
- Rede WiFi 2.4 GHz compartilhada entre placas e servidor

---

## Compilação do firmware

O firmware usa três targets distintos, um por placa, com `NODE_ID`
definido em compile-time.

```bash
mkdir build && cd build
cmake .. -DSSID="sua_rede" -DPASSWORD="sua_senha" -DDEV_SERVER_IP="192.168.x.x"
cmake --build . --target all
```

Isso gera `avocado_node0.uf2`, `avocado_node1.uf2` e `avocado_node2.uf2`
em `dist/`. Grave cada arquivo na placa correspondente segurando BOOTSEL
ao conectar via USB.

Credenciais de rede não entram no controle de versão — defina em
`secrets.h` (ignorado pelo `.gitignore`) ou passe via flags de
compilação como mostrado acima.

---

## Painel administrativo

O servidor expõe uma rota `/admin` com:

- Simulação de latência de processamento (slider 0–2000 ms)
- Forçar um nó como offline para testar reconexão
- Reset de sessão sem reiniciar o servidor

---

## Fluxo de uma sessão

1. Ligue as três placas. Elas descobrem o servidor via UDP e se registram.
2. Pressione o botão A para acumular cliques. O Core 0 os envia a cada 20 ms.
3. Pressione o botão B para ativar o multiplicador ×3 por 10 segundos.
4. Desligue o servidor — as placas entram em OFFLINE e continuam acumulando.
5. Religue o servidor — cada placa sincroniza os cliques pendentes via `sync_offline`.
6. O dashboard reflete tudo em menos de 1 segundo via WebSocket.

---

## Estrutura do repositório

```
firmware/
  main.c                entry point — boot e loop principal do Core 0
  rpc_client.c/h        API pública de rede — único ponto de lwIP
  middleware/
    lamport.c/h         relógio de Lamport thread-safe
    shared_state.c/h    estado compartilhado entre cores (spinlock)
  drivers/
    oled.c/h            driver SSD1306
    led_matrix.c/h      WS2812 via PIO
  audio/
    buzzer_queue.c/h    fila de eventos sonoros (capacidade 4)
  discovery/
    service_disc.c      broadcast UDP de descoberta
  hardware_config.h     único arquivo com números de pino e NODE_ID

server/                 stack a definir

dist/                   binários gerados (.uf2) — não versionado
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

Etiquete fisicamente cada placa antes da apresentação.

