Aqui estão as implementações solicitadas para fechar a US de Consumo/Restauração Confiável de Cliques, focando estritamente na observabilidade (logs e cooldown) e na documentação (plano de testes e correção arquitetural).

### 1. Modificações em `firmware/main.c`

**Por que:** Esta mudança adiciona vibilidade empírica ao crescimento monotônico do `pending_clicks` através do polling no loop principal, além de injetar um `RPC_COOLDOWN_MS` de 1 segundo após falhas, permitindo ao testador esmagar o botão e ver o batch aglutinar cliques antigos e novos antes do próximo take.

**Como testar manualmente:** 
Pressionar o botão repetidas vezes. A cada 3 envios (regra do stub), o log `[RPC_FAIL] Restore crítico...` aparecerá. Nesse exato segundo de cooldown, continue clicando rápido. Você verá logs como `[BTN_MONITOR] pending_clicks cresceu para: 2`, `4`, `7`. Quando o cooldown expirar, o log `[RPC_TX]` vai consumir o lote de 7 de uma vez só, provando o merge sem perdas.

```diff
--- a/firmware/main.c
+++ b/firmware/main.c
@@ -82,34 +82,53 @@
 
   printf("\n[MAIN] Entrando no loop principal...\n");
 
+  static uint32_t last_logged_pending = 0;
+  static uint32_t ms_since_last_rpc_attempt = 0;
+  const uint32_t RPC_COOLDOWN_MS = 1000; // 1s de cooldown após falha
+
   while (true) {
-    // Core 0 consome cliques pendentes de forma atômica (US-04)
-    uint32_t batch_clicks = shared_state_take_pending_clicks();
-
-    if (batch_clicks > 0) {
-      printf("[BTN] Batch taken: %u\n", batch_clicks);
-
-      if (send_clicks_rpc(batch_clicks)) {
-        // Sucesso: atualiza placar e feedback
-        local_score_confirmed += batch_clicks;
-        shared_state_set_local_score(local_score_confirmed);
-
-        printf("[BTN] RPC OK | Total Confirmed: %u\n", local_score_confirmed);
-
-        // Sinaliza eventos para o Core 1
-        if (local_score_confirmed % 10 == 0 && local_score_confirmed > 0) {
-          shared_state_set_milestone_triggered(true);
+    uint32_t current_pending = shared_state_get_pending_clicks();
+
+    if (current_pending > last_logged_pending && current_pending > 0) {
+      printf("[BTN_MONITOR] pending_clicks cresceu para: %u\n", current_pending);
+      last_logged_pending = current_pending;
+    } else if (current_pending == 0) {
+      last_logged_pending = 0;
+    }
+
+    if (ms_since_last_rpc_attempt >= RPC_COOLDOWN_MS) {
+      // Core 0 consome cliques pendentes de forma atômica (US-04)
+      uint32_t batch_clicks = shared_state_take_pending_clicks();
+
+      if (batch_clicks > 0) {
+        printf("[RPC_TX] Consumindo batch atômico de %u cliques. Iniciando envio...\n", batch_clicks);
+
+        if (send_clicks_rpc(batch_clicks)) {
+          // Sucesso: atualiza placar e feedback
+          local_score_confirmed += batch_clicks;
+          shared_state_set_local_score(local_score_confirmed);
+
+          printf("[RPC_OK] Placar confirmado atualizado para: %u\n", local_score_confirmed);
+
+          // Sinaliza eventos para o Core 1
+          if (local_score_confirmed % 10 == 0 && local_score_confirmed > 0) {
+            shared_state_set_milestone_triggered(true);
+          } else {
+            shared_state_set_led_flash_requested(true);
+          }
+          buzzer_tone(2000, 20);
         } else {
-          shared_state_set_led_flash_requested(true);
+          // Falha: restaura cliques para o próximo batch (merge)
+          shared_state_restore_clicks(batch_clicks);
+          printf("[RPC_FAIL] Restore crítico: devolvendo %u cliques ao pool. (Merge)\n", batch_clicks);
+          
+          ms_since_last_rpc_attempt = 0; // Inicia cooldown
+
+          // Opcional: feedback de erro (tom mais grave)
+          buzzer_tone(500, 50);
         }
-        buzzer_tone(2000, 20);
-      } else {
-        // Falha: restaura cliques para o próximo batch (merge)
-        shared_state_restore_clicks(batch_clicks);
-        printf("[BTN] RPC FAIL | Restored %u to pending\n", batch_clicks);
-
-        // Opcional: feedback de erro (tom mais grave)
-        buzzer_tone(500, 50);
       }
+    } else {
+      ms_since_last_rpc_attempt += 10;
     }
 
```

### 2. Modificações na Documentação (`docs/consumo-cliques-confiavel.md`)

**Por que:** A US estava erroneamente nomeada ("pelo Core 1"), quando na verdade acontece via IRQ no ecossistema do Core 0. Além da correção de título e estruturação de Atores, a adição do plano de testes operacionaliza os critérios de aceite.

**Como testar (documentação):** Não há execução de código, mas ao ler o doc os desenvolvedores agora terão os playbooks `Cenário 1` e `Cenário 2` para validar a funcionalidade localmente a qualquer momento.

```diff
--- a/docs/consumo-cliques-confiavel.md
+++ b/docs/consumo-cliques-confiavel.md
@@ -1,4 +1,4 @@
-# Consolidação e Restauração de Cliques (US-04)
+# Coleta e Restauração Atômica de pending_clicks pela Lógica de Jogo (Core 0)
 
 Este documento descreve a implementação da User Story 04, que garante a integridade dos cliques registrados pelo usuário, mesmo em condições de instabilidade na rede.
 
@@ -6,6 +6,12 @@
 
 ## Aspectos de Implementação
 
+## Arquitetura e Atores
+
+*   **IRQ (Core 0):** Produtora de cliques. Incrementa `pending_clicks`.
+*   **Loop Principal (Core 0):** Consumidor e Restaurador. Usa `take` para consumir um lote e tentar o envio RPC, e `restore` em caso de falha.
+*   **Core 1:** Somente Apresentação. Lê o estado para atualizar o display, sem alterar dados da lógica do jogo.
+
 ### 1. Registro Sem Perdas (IRQ)
 A interrupção de hardware do botão A incrementa o campo `pending_clicks` no Estado Compartilhado.
 * **API**: `shared_state_increment_pending_clicks()`
@@ -47,4 +53,28 @@
 ## Invariantes
 1. `local_score` só aumenta após a confirmação de recebimento pelo servidor.
 2. A soma de `local_score + pending_clicks` (em um dado instante) representa o total real de cliques realizados no dispositivo.
+
+## Plano de Testes e Aceite
+
+Para validar que a funcionalidade foi implementada corretamente e atende aos critérios de aceite, execute os seguintes cenários de teste manual observando o log serial e o display OLED.
+
+### Cenário 1: Sucesso Simples (Caminho Feliz)
+*   **Objetivo:** Validar o consumo de cliques sem perda ou dupla contagem quando a rede está funcional.
+*   **Ação:** Com a placa conectada, pressione o botão A 5 vezes seguidas, com pausas curtas (aprox. 500ms) entre os cliques, fora de um período de falha simulada.
+*   **Observação Esperada (Logs):**
+    *   Surgirão logs `[RPC_TX] Consumindo batch atômico de 1 cliques...` repetidas vezes.
+    *   Seguidos por `[RPC_OK] Placar confirmado atualizado para: X` (crescendo linearmente).
+*   **Observação Esperada (Apresentação):**
+    *   O `local_score` no display (gerenciado pelo Core 1) crescerá monotonicamente até 5, sem pular números.
+
+### Cenário 2: Falha RPC e Merge de Cliques (Recuperação)
+*   **Objetivo:** Validar que nenhum clique é perdido durante uma instabilidade de rede e que o `pending_clicks` acumula monotonicamente.
+*   **Ação:** Sabendo que o simulador (`send_clicks_rpc`) falha a cada 3 tentativas, inicie uma sequência rápida de cliques no momento em que uma falha ocorrer (momento do "cooldown" de 1s). Clique cerca de 7 vezes rapidamente.
+*   **Observação Esperada (Logs):**
+    *   Um erro inicial: `[RPC_FAIL] Restore crítico...` e início do cooldown de 1s.
+    *   Durante a "rajada" de cliques no cooldown, os logs mostrarão o crescimento monotônico: `[BTN_MONITOR] pending_clicks cresceu para: 2... 4... 7...`
+    *   Quando o cooldown acabar, um único log de envio englobando tudo: `[RPC_TX] Consumindo batch atômico de X cliques. Iniciando envio...` (onde X será o número exato de cliques armazenados durante o cooldown mais o lote restaurado).
+*   **Observação Esperada (Apresentação):**
+    *   O display congelará o `local_score` durante a falha. Após o término do cooldown e o sucesso do próximo RPC, o placar dará um salto (ex: de 2 para 9), refletindo a consolidação atômica e o recebimento de todos os eventos sem perda de frame. A contagem total física de apertos de botão DEVE bater com o display final.
 
```
