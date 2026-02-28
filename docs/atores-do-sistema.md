O firmware tem três atores principais que competem pelo estado do sistema, cada um tem sua função e restrição

1. IRQ do Botão A
  * É uma função de hardware ativada automaticamente quando o botão A é pressionado. Ela não é chamada por ninguem e interrompe o que estiver acontecendo.
  * Restrições: Deve ser rápida, cada desperdicio é comido do tempo do loop principal. Não pode chamar funções que possam bloquear ou demorar muito tempo. 

    pending_clicks += 1 (via shared_state_increment_pending_clicks  Protegido por Spinlock)

2. Loop Principal
  * É o while(1) do main.c, onde o programa passa a maior parte do tempo. É o único ator que conhece a rede e roda continuamente.
  * Cada ciclo coleta os cliques, aplica o timestamp Lamport e os envia ao servidor via RPC, também gerencia Wifi, reconexões, e outras tarefas.
  * Restrições: Não pode acessar OLED, led ou buzzer diretamente.
  * Pode ser interrompido pela IRQ do botão A.

    Ações no estado (via API shared_state_*):

    lê e zera pending_clicks (via shared_state_take_pending_clicks)
    escreve global_score (via shared_state_set_global_score)
    escreve local_score (via shared_state_set_local_score)
    escreve node_scores[] (via shared_state_set_node_scores)
    seta connection_status (via shared_state_set_connection_status)
    gerencia current_lamport_ts (via shared_state_increment_lamport_ts)

3. Loop de Apresentação
    * É o loop que atualiza o display OLED, mostrando o placar, status de conexão, e outras informações. Roda em paralelo real com o Core 1, não é uma tarefa agendada, é um segundo processo rodando ao mesmo tempo.
    * A cada 100ms, ele lê o estado do sistema (placar, status de conexão, etc) e atualiza o display, não toma nenhuma decisão, apenas apresenta o estado atual.
    * Restrições: Nunca escreve em pending_clicks, ou qualquer campo de lógica de jogo. Ele só pode ler o estado do sistema e atualizar o display.

    Ações no estado (via API shared_state_*):

    lê global_score + local_score + node_scores[] (via getters protegidos)
    lê connection_status (via shared_state_get_connection_status)
    consome milestone_triggered (via shared_state_get/set_milestone_triggered)
    consome led_flash_requested (via shared_state_get/set_led_flash_requested)

Fluxo de dados:
Botão A pressionado -> IRQ -> shared_state_increment_pending_clicks() -> Aquisição de Spinlock -> Clique armazenado -> Loop Principal (Core 0) -> shared_state_take_pending_clicks() -> aquisição segura -> zeramento atômico -> atualiza placar e envia ao servidor -> recebe resposta -> atualiza scores/status -> Loop de Apresentação (Core 1) -> lê scores/status via getters -> atualiza display, etc.

Não é apenas organização já que se o OLED apresentar algum valor errado o bug está no core 1 ou na leitura do spinlock, se o clique se perder o bug esta no IRQ ou no clicks do Core 0, se a rede trava o bug está no core 0. Com a API centralizada e o Spinlock de hardware, isolamos completamente o estado das condições de corrida entre núcleos.