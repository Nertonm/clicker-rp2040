O firmware tem três atores principais que competem pelo estado do sistema, cada um tem sua função e restrição

1. IRQ do Botão A
  * É uma função de hardware ativada automaticamente quando o botão A é pressionado. Ela não é chamada por ninguem e interrompe o que estiver acontecendo.
  * Restrições: Deve ser rápida, cada desperdicio é comido do tempo do loop principal. Não pode chamar funções que possam bloquear ou demorar muito tempo. 

    pending_clicks += 1 (via __atomic_fetch_add)

2. Loop Principal
  * É o while(1) do main.c, onde o programa passa a maior parte do tempo. É o único ator que conhece a rede e roda continuamente.
  * Cada ciclo coleta os cliques, aplica o timestamp Lamport e os envia ao servidor via RPC, também gerencia Wifi, reconexões, e outras tarefas.
  * Restrições: Não pode acessar OLED, led ou buzzer diretamente.
  * Pode ser interrompido pela IRQ do botão A.

    Ações no estado:

    lê e zera pending_clicks  (via __atomic_exchange — operação indivisível)
    escreve global_score (via spinlock — junto com local_score)
    escreve local_score (via spinlock — junto com global_score)
    escreve node_scores[] (via spinlock — junto com os scores acima)
    seta connection_status (via __atomic_store)
    consome powerup_requested (via __atomic_exchange — lê e limpa junto)

3. Loop de Apresentação
    * É o loop que atualiza o display OLED, mostrando o placar, status de conexão, e outras informações. Roda em paralelo real com o Core 0, não é uma tarefa agendada, é um segundo processo rodando ao mesmo tempo.
    * A cada 100ms, ele lê o estado do sistema (placar, status de conexão, etc) e atualiza o display, não toma nenhuma decisão, apenas apresenta o estado atual.
    * Restrições: Nunca escreve em pending_clicks, ou qualquer campo de lógica de jogo, nem chama funções de rede, já que o CYW43 não é thread-safe. Ele só pode ler o estado do sistema e atualizar o display. Consome flags e limpa dps.

    lê global_score + local_score + node_scores[] (via spinlock — lê como bloco)
    lê connection_status (via __atomic_load)
    lê debug_mode  (via __atomic_load)
    consome milestone_triggered  (via __atomic_exchange)
    consome powerup_confirmed (via __atomic_exchange)

Fluxo de dados:
Botão A pressionado -> IRQ (Core 0 interrompido) -> pending_clicks++ -> Loop Principal (Core 0) -> lê e zera pending_clicks -> atualiza placar e envia ao servidor via RPC -> recebe resposta do servidor -> atualiza status de conexão + scores -> loop de apresentação (Core 1) -> lê scores e status -> atualiza display, etc.

Não é apenas organização já que se o OLED apresentar algum valor errado o bug está no core 1 ou na leitura do spinlock, se o clique se perder o bug esta no IRQ ou no clicks do Core 0, se a rede trava o bug está no core 0. Sem essa speração qualpor bug poderia estar em qualquer lugar, com essa separação fica mais fácil de isolar e corrigir.