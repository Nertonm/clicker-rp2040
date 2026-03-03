/**
 * @file firmware.c
 * @brief Firmware de exemplo e testes unitários de periféricos.
 *
 * Este arquivo contém exemplos de uso de PIO para blink, configuração
 * manual de WiFi e testes de comunicação UART.
 *
 * @note Este arquivo não é o ponto de entrada principal da aplicação final.
 */

#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/uart.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include <stdio.h>

#define I2C_PORT i2c0
#define I2C_SDA 8
#define I2C_SCL 9

#include "blink.pio.h"

/**
 * @brief Inicia um programa PIO de blink em um pino específico.
 *
 * @param[in] pio Instância do PIO (0 ou 1).
 * @param[in] sm Máquina de estado (0 a 3).
 * @param[in] offset Endereço do programa na memória de instruções do PIO.
 * @param[in] pin Pino GPIO alvo.
 * @param[in] freq Frequência desejada de piscagem em Hz.
 */
void blink_pin_forever(PIO pio, uint sm, uint offset, uint pin, uint freq) {
  blink_program_init(pio, sm, offset, pin);
  pio_sm_set_enabled(pio, sm, true);

  printf("Blinking pin %d at %d Hz\n", pin, freq);

  /* Envia o valor do contador para a FIFO do PIO */
  pio->txf[sm] = (125000000 / (2 * freq)) - 3;
}

#define UART_ID uart1
#define BAUD_RATE 115200
#define UART_TX_PIN 4
#define UART_RX_PIN 5

/**
 * @brief Loop de teste de hardware.
 *
 * @return int Status de execução.
 */
int main() {
  stdio_init_all();

  /* Inicialização do chip WiFi */
  if (cyw43_arch_init()) {
    printf("Wi-Fi init failed\n");
    return -1;
  }

  /* Configuração do barramento I2C0 */
  i2c_init(I2C_PORT, 400 * 1000);
  gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(I2C_SDA);
  gpio_pull_up(I2C_SCL);

  /* Exemplo de uso de PIO */
  PIO pio = pio0;
  uint offset = pio_add_program(pio, &blink_program);
  printf("Loaded program at %d\n", offset);

#ifdef PICO_DEFAULT_LED_PIN
  blink_pin_forever(pio, 0, offset, PICO_DEFAULT_LED_PIN, 3);
#else
  blink_pin_forever(pio, 0, offset, 6, 3);
#endif

  /* Conexão manual ao WiFi */
  cyw43_arch_enable_sta_mode();
  printf("Connecting to Wi-Fi...\n");
  if (cyw43_arch_wifi_connect_timeout_ms("Your Wi-Fi SSID",
                                         "Your Wi-Fi Password",
                                         CYW43_AUTH_WPA2_AES_PSK, 30000)) {
    printf("failed to connect.\n");
    return 1;
  } else {
    printf("Connected.\n");
    uint8_t *ip_address = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    printf("IP address %d.%d.%d.%d\n", ip_address[0], ip_address[1],
           ip_address[2], ip_address[3]);
  }

  /* Configuração da UART1 */
  uart_init(UART_ID, BAUD_RATE);
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  uart_puts(UART_ID, " Hello, UART!\n");

  while (true) {
    printf("Hello, world!\n");
    sleep_ms(1000);
  }
}
