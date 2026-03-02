# Estado Compartilhado (Shared State)

O Estado Compartilhado é o componente de middleware responsável por centralizar e sincronizar os dados trocados entre IRQs e tasks FreeRTOS (`task_buttons`, `task_rpc`, `task_display` e `task_monitor`).

1. Estrutura Centralizada (`shared_state_t`)
   * Todos os dados voláteis do sistema residem em uma estrutura única, garantindo que não haja variáveis globais espalhadas pelo código.
   * Campos atuais incluem:
     - Cliques e placar: `pending_clicks`, `local_score`, `global_score`, `node_scores[]`
     - Conectividade e erro: `connection_status`, `fallback_in_use`, `server_error_active`
     - Sinalização visual/eventos: `milestone_triggered`, `led_flash_requested`
     - Turbo: `pending_turbo_activations`, `turbo_active`, `turbo_until_ms`
     - Lamport local legado: `current_lamport_ts`

2. Sincronização via Spinlock (Hardware)
   * Utiliza um **Spinlock de Hardware** do RP2040 (alocado dinamicamente via `spin_lock_claim_unused`) para garantir atomicidade em todas as operações de leitura e escrita.
   * Restrições: Toda interação com o estado DEVE ser feita através das funções de API (`shared_state_get_*` e `shared_state_set_*`). Operações diretas na estrutura são proibidas fora do módulo `shared_state.c`.
   * Bloqueio: O uso de `spin_lock_blocking` garante que, se um core estiver escrevendo, o outro aguardará a liberação, evitando corrupção de memória ou condições de corrida.

3. Fluxo de Consumo Atômico
   * Para campos sensíveis como `pending_clicks`, a API fornece `shared_state_take_pending_clicks()`, que lê e zera o contador em uma única sessão crítica.
   * Para eventos one-shot, há funções `take` específicas (`shared_state_take_milestone_triggered`, `shared_state_take_led_flash_requested`, `shared_state_take_turbo_activation_requested`) que consomem o evento de forma atômica.

5. Leitura Atômica Agrupada (Snapshot)
   * Visando a eliminação completa de tearings ou dessincronizações visuais, a API provê `shared_state_get_display_snapshot()`.
   * Essa função engloba e copia múltiplos estados (scores, status da rede e cliques pendentes) em uma única estrutura `display_snapshot_t` sob a proteção de apenas um *lock/unlock*.

6. Recuperação em Caso de Falha (Take-and-Restore)
   * Visando a confiabilidade na rede, o sistema implementa uma semântica de "restauração": se o Core 0 consome um lote de cliques mas falha ao enviá-los ao servidor (RPC), ele utiliza `shared_state_restore_clicks(n)` para devolver esses cliques ao pool de pendentes.
   * Esses cliques restaurados são somados (merge) atomicamente a quaisquer novos cliques que tenham chegado via IRQ no intervalo, garantindo que o placar local e global eventualmente reflitam a realidade sem perdas.

7. Fluxo do Turbo (botão B)
   * A IRQ do botão B chama `shared_state_request_turbo_activation()`.
   * A `task_rpc` consome com `shared_state_take_turbo_activation_requested()` e ativa turbo online (`rpc_activate_powerup`) ou local/offline.
   * A janela ativa é mantida por `shared_state_set_turbo_active(true)` e `shared_state_set_turbo_until_ms(...)`.
   * A `task_display` lê `turbo_active`/`turbo_until_ms` para renderizar arco-íris e expirar o efeito.

Fluxo de Dados Protegido:
Evento (IRQ) -> `shared_state` -> task consumidora (`buttons`/`rpc`/`display`) -> update de estado -> renderização/telemetria.
