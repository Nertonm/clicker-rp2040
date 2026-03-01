# Matriz de LEDs WS2812 (PIO)

A matriz de 25 LEDs (5x5) é o feedback visual principal do firmware. Ela mostra o dígito do placar local, destaca milestones e sinaliza turbo.

## Driver e hardware

1. Driver PIO (Hardware)
   * Usa PIO para gerar sinal WS2812 em 800kHz.
   * O protocolo é enviado em GRB com latch no final de cada frame.

2. Driver C (`drivers/display/ws2812`)
   * Mantém buffer local de 25 pixels.
   * API principal:
     - `led_matrix_init()`
     - `led_set(index, r, g, b)`
     - `led_clear_all()`
     - `led_matrix_draw_number(num, r, g, b)`
     - `led_matrix_set_all(r, g, b)`

3. Pinagem
   * Definida em `hardware_config.h` como `WS2812_PIN` (GPIO 7).

## Estados visuais atuais

- Estado normal
  * Mostra `local_score % 10` em branco suave.

- Clique (feedback rápido)
  * Pisca o LED central em azul curto.

- Turbo (botão B)
  * Enquanto ativo, o dígito muda para paleta arco-íris animada.
  * No OLED, a linha de status mostra `TURBO x3`.

- Milestone
  * Ao cruzar múltiplos de 10 cliques locais (e também em confirmação de milestone), executa duas piscadas douradas na matriz.
  * Em seguida, mantém o dígito em dourado por alguns frames.

## Fluxo de dados visual

Mudança de estado (`shared_state`) -> `task_display` -> Driver WS2812 -> FIFO PIO -> GPIO 7 -> Matriz física.
