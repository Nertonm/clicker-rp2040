# Atores do Sistema (FreeRTOS)

Este documento descreve os atores concorrentes reais do firmware após a migração para FreeRTOS.

## 1) IRQ de Botão A (GPIO)

- Disparada por borda de descida no botão A.
- Responsabilidade: incrementar `pending_clicks` em `shared_state`.
- Restrição: rotina curta e não-bloqueante.

## 2) IRQ de Botão B (GPIO)

- Disparada por borda de descida no botão B.
- Responsabilidade: sinalizar pedido de turbo (`shared_state_request_turbo_activation`).
- Restrição: rotina curta e não-bloqueante.

## 3) `task_buttons` (Priority 3)

- Consome `pending_clicks` do estado compartilhado.
- Atualiza placar local imediatamente para feedback responsivo.
- Publica lotes na `queue_clicks` para envio assíncrono via RPC.

## 4) `task_rpc` (Priority 2)

- Inicializa CYW43/lwIP e registra o nó no servidor.
- Envia batches de clique via `rpc_add_clicks` e aplica atualização confirmada.
- Processa pedidos de turbo (botão B):
  - online: tenta `rpc_activate_powerup`
  - offline: aplica turbo local com janela temporal
- Executa `rpc_poll` para reconciliação de fila offline.

## 5) `task_display` (Priority 1)

- Lê estado compartilhado e renderiza OLED.
- Controla matriz WS2812:
  - dígito normal (branco)
  - flash azul em clique
  - modo turbo com arco-íris animado
  - milestone com flash dourado + brilho dourado temporário

## 6) `task_monitor` (Priority 1)

- Telemetria periódica (`queue`, heap livre e heap mínimo).
- Sem impacto direto na lógica de jogo.

## Sincronização e Ownership

- `shared_state`: protegido por spinlock.
- `queue_clicks`: canal de produção/consumo entre `task_buttons` e `task_rpc`.
- Lamport clock: atualizado no caminho RPC de envio/recebimento.

## Fluxo resumido

Botão A/B -> IRQ -> `shared_state` -> `task_buttons`/`task_rpc` -> RPC -> `shared_state` -> `task_display`/OLED+WS2812.
