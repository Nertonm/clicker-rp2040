# Matriz de LEDs WS2812 (PIO)

A matriz de 25 LEDs (5x5) é o componente de feedback visual em tempo real, utilizada para sinalizar estados como cliques, marcos (milestones) e status de conexão.

1. Driver PIO (Hardware)
   * Utiliza o periférico **PIO1** do RP2040 para gerar sinais de timing precisos de 800kHz, garantindo que o protocolo WS2812 (NeoPixel) funcione sem falhas.
   * Restrições: Usa o **PIO1** exclusivamente para evitar conflitos com o driver de Wi-Fi do Pico W (CYW43), que utiliza o PIO0. O timing é determinístico e independente da carga de processamento dos Cores.

2. Driver C (drivers/display/ws2812)
   * Gerencia a interface de software, mantendo um buffer local de pixels para permitir atualizações parciais ou totais.
   * **Controle de Brilho**: Implementa uma redução global de 16x via bit-shift (`>> 4`) antes do envio ao PIO, garantindo conforto visual no hardware BitDogLab.
   * Funções principais: `led_matrix_init()`, `led_set(index, r, g, b)`, `led_matrix_draw_number(num, r, g, b)` e `led_matrix_set_all(r, g, b)`.
   * Restrições: Cada atualização envia a sequência completa de 25 pixels (GRB) para o hardware e aguarda um pulso de reset de ~300µs para garantir que os LEDs processem a cor.

3. Integração com o Sistema
   * Devido à sua natureza de apresentação, a matriz deve preferencialmente ser atualizada pelo **Loop de Apresentação (Core 1)** para evitar atrasos no **Loop Principal (Core 0)**.
   * Pinagem: Declarada em `hardware_config.h` como `WS2812_PIN` (GPIO 7).

Fluxo de Dados Visual:
Mudança de Estado (Placar/Erro) -> Driver WS2812 -> Buffer de Pixels -> Fila (FIFO) do PIO1 -> Output GPIO 7 -> Matriz física.
