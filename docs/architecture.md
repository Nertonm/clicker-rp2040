# Arquitetura do Firmware

## Arquitetura com FreeRTOS

### Camadas

**`rpc_client.h/c`** - Interface única de rede
- Encapsula lwIP, sockets e JSON-RPC
- Exposição de API sem tipos lwIP no header público
- Reconexão automática e fila offline interna

**`main.c`** - Orquestração de runtime
- Inicialização de hardware e rede
- Criação das tasks FreeRTOS
- Integração entre botões, RPC, display e monitoria
- Processamento de turbo local (botão B) e feedback visual na matriz

**`middleware/shared_state.*`** - Estado compartilhado thread-safe
- Scores e status de conexão
- Flags de evento (milestone, flash LED)
- Sinalização de turbo (request, ativo, expiração)
- Proteção com spinlock

### Modelo de Tasks

```text
Priority 5: lwIP/CYW43 task (SDK)
Priority 3: task_buttons   -> lê pending_clicks para gerar feedback (buzzer/LED) sem consumi-los
Priority 2: task_rpc       -> registra nó, consome pending_clicks nativamente, envia batches e sincroniza
Priority 1: task_display   -> renderiza estado no OLED e na matriz WS2812
Priority 1: task_monitor   -> diagnóstico de heap/fila/conectividade
```

### Sincronização

| Recurso | Mecanismo | Escrita principal | Leitura principal |
|---------|-----------|-------------------|-------------------|
| `shared_state` | spinlock | IRQs + `task_rpc` | `task_display`/`task_monitor`/`task_buttons` |
| Lamport clock | critical section | `task_rpc` | `task_rpc` |

### Fluxo de dados

1. IRQ de botão incrementa `pending_clicks` no shared state.
2. `task_buttons` detecta crescimento em `pending_clicks` e dispara beep curto e alerta visual (LED) imediatamente.
3. IRQ do botão B marca pedido de turbo em `shared_state`.
4. `task_rpc` consome diretamente de `pending_clicks`, chama `rpc_add_clicks`, atualiza Lamport e scores.
5. `task_rpc` processa pedido de turbo com `rpc_activate_powerup` (ou fallback local offline).
6. `task_rpc` chama `rpc_poll` para drenar fila offline quando reconectar.
7. `task_display` lê `shared_state` e atualiza OLED + matriz WS2812.

### Orçamento de memória (referência)

```text
Heap FreeRTOS configurado: 80 KB
Uso típico após boot:       ~25-35 KB
Folga típica:               ~45 KB+
```

### Regra de encapsulamento de rede

- `rpc_client.c` é o único módulo geral autorizado a usar sockets TCP diretamente.
- Discovery UDP permanece isolado em `discovery/service_disc.c`.
- Qualquer fluxo de negócio novo deve usar `rpc_client.h` em vez de incluir headers lwIP.

### Guia rápido para nova task

1. Definir função `task_*` com loop e `vTaskDelay`/`vTaskDelayUntil`.
2. Criar task em `main` com stack/prioridade adequados.
3. Integrar dados via queue/evento/shared_state (evitar variáveis globais sem proteção).
4. Monitorar watermark de stack e heap mínimo em runtime.
