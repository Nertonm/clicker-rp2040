# Estado Compartilhado (Shared State)

O Estado Compartilhado é o componente de middleware responsável por centralizar e sincronizar todos os dados trocados entre o **Core 0 (Lógica/Rede)** e o **Core 1 (Apresentação)**, além de processar eventos assíncronos das IRQs.

1. Estrutura Centralizada (`shared_state_t`)
   * Todos os dados voláteis do sistema residem em uma estrutura única, garantindo que não haja variáveis globais espalhadas pelo código.
   * Campos incluídos: `pending_clicks`, `local_score`, `global_score`, `node_scores[]`, `connection_status`, `current_lamport_ts`, `milestone_triggered` e `led_flash_requested`.

2. Sincronização via Spinlock (Hardware)
   * Utiliza um **Spinlock de Hardware** do RP2040 (alocado dinamicamente via `spin_lock_claim_unused`) para garantir atomicidade em todas as operações de leitura e escrita.
   * Restrições: Toda interação com o estado DEVE ser feita através das funções de API (`shared_state_get_*` e `shared_state_set_*`). Operações diretas na estrutura são proibidas fora do módulo `shared_state.c`.
   * Bloqueio: O uso de `spin_lock_blocking` garante que, se um core estiver escrevendo, o outro aguardará a liberação, evitando corrupção de memória ou condições de corrida.

3. Fluxo de Consumo Atômico
   * Para campos sensíveis como `pending_clicks`, a API fornece a função `shared_state_take_pending_clicks()`, que lê o valor atual e zera o contador em uma única sessão crítica, garantindo que nenhum clique seja processado duas vezes ou perdido entre a leitura e a limpeza.

4. Recuperação em Caso de Falha (Take-and-Restore)
   * Visando a confiabilidade na rede, o sistema implementa uma semântica de "restauração": se o Core 0 consome um lote de cliques mas falha ao enviá-los ao servidor (RPC), ele utiliza `shared_state_restore_clicks(n)` para devolver esses cliques ao pool de pendentes.
   * Esses cliques restaurados são somados (merge) atomicamente a quaisquer novos cliques que tenham chegado via IRQ no intervalo, garantindo que o placar local e global eventualmente reflitam a realidade sem perdas.

Fluxo de Dados Protegido:
Evento (IRQ) -> Incremento -> Take (Core 0) -> Tentativa RPC -> [Sucesso: Confirma Placar | Falha: Restore -> Merge] -> Get (Core 1) -> Apresentação.
