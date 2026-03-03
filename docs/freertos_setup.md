# Arquitetura FreeRTOS (RP2040)

Este documento descreve a implementação e o uso do FreeRTOS no firmware do Clicker RP2040.

## Visão Geral

O sistema utiliza o FreeRTOS para gerenciar tarefas concorrentes, garantindo que a leitura de botões, a comunicação de rede e a atualização do display ocorram sem bloqueios mútuos.

## Tasks e Prioridades

As tarefas são criadas no `main.c` com as seguintes prioridades (maior número = maior prioridade):

| Task | Prioridade | Stack | Função |
| :--- | :--- | :--- | :--- |
| `buttons` | 3 | 512 | Leitura de GPIO e debounce de botões. |
| `rpc` | 2 | 2048 | Gerenciamento de Wi-Fi e chamadas JSON-RPC. |
| `display` | 1 | 1024 | Atualização do OLED e Matriz de LEDs. |
| `monitor` | 1 | 768 | Telemetria de heap e status de filas. |

## Comunicação entre Tasks

### Estado Distribuído (`shared_state`)
A comunicação principal entre a entrada do usuário e a rede é feita via spinlock na RAM:
- **Produtor:** ISR do botão incrementa `pending_clicks`.
- **Consumidor:** `task_rpc` consome os cliques diretamente para enviá-los em batches.
- **Vantagem:** Evita cópias desnecessárias e permite rollback limpo (restaurar de volta na variável) se a rede cair.

## Sincronização e Thread-Safety

Embora as tasks rodem em um único core (atualmente), o sistema utiliza **Spinlocks de Hardware** do RP2040 via `middleware/shared_state.c`.

- **Mecanismo:** `shared_state` encapsula todas as variáveis globais.
- **Hardware Sync:** Usa `spin_lock_blocking` e `spin_unlock` para acesso atômico.
- **Inter-Core:** Esta arquitetura permite uma migração futura para SMP (Symmetric Multiprocessing) sem refatoração da lógica de proteção de dados.

## Tratamento de Erros (Hooks)

O firmware implementa hooks para detectar falhas críticas em tempo real:

1. **Stack Overflow:** `vApplicationStackOverflowHook` interrompe a execução e imprime a task agressora via UART.
2. **Falha de Malloc:** `vApplicationMallocFailedHook` notifica se o heap de 80KB for exaurido.
3. **Asserts:** `configASSERT` é usado em inicializações críticas (como criação de filas e tasks).

## Configurações Principais (`FreeRTOSConfig.h`)

- **Tick Rate:** 1000 Hz (1ms resolution).
- **Heap Size:** 80 KB (`heap_4.c`).
- **Preemption:** Habilitada (`configUSE_PREEMPTION = 1`).
