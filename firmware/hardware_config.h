#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

// Botões (pull-up interno, estado padrão HIGH, pressionado = LOW)
#define BTN_A_PIN           10
#define BTN_B_PIN            5
#define BTN_C_PIN            6


// LED RGB (cátodo comum)
// R: 220 ohm — G: 220 ohm — B: 150 ohm
#define LED_RGB_RED_PIN     13
#define LED_RGB_GREEN_PIN   11
#define LED_RGB_BLUE_PIN    12


// Matriz de LEDs WS2812B 5×5 (NeoPixel)
// Índice 0 = canto inferior direito, índice 24 = canto superior esquerdo
//
//  [24][23][22][21][20]   ← linha 0 (topo)
//  [15][16][17][18][19]   ← linha 1
//  [14][13][12][11][10]   ← linha 2
//  [ 5][ 6][ 7][ 8][ 9]  ← linha 3
//  [ 4][ 3][ 2][ 1][ 0]  ← linha 4 (base)
#define WS2812_PIN           7
#define WS2812_NUM_LEDS     25
#define WS2812_IS_RGBW   false


// Buzzer passivo (via transistor)
#define BUZZER_A_PIN        21


// Display OLED — I2C1 (SDA=GP2, SCL=GP3)
// V7 pode ter SSD1306 (128×64) ou SH1107 (128×128)
// Endereço padrão: 0x3C
#define OLED_SDA_PIN         2
#define OLED_SCL_PIN         3
#define OLED_I2C_PORT       i2c1
#define OLED_I2C_ADDR     0x3C
#define OLED_I2C_FREQ   400000


// Joystick analógico KY-023
// pull-up interno no botão SW
#define JOY_VRX_PIN         27      // ADC1
#define JOY_VRY_PIN         26      // ADC0
#define JOY_SW_PIN          22      // pull-up, pressionado = LOW


// Microfone de eletreto (saída analógica)
// Nível DC em repouso: ~1,65 V (valor ADC ~2047 em 12 bits)
#define MIC_PIN             28      // ADC2


// 
// I2C0 — conector lateral direito (GPIO0/GPIO1)
// Uso: sensores externos, GPS (UART), dispositivos I2C secundários
#define I2C0_SDA_PIN         0
#define I2C0_SCL_PIN         1
#define I2C0_PORT           i2c0


// Conector IDC 14 pinos — expansão de hardware
// Pinos disponíveis para uso geral ou placas de extensão (BitMovel, LoRa)
#define IDC_GPIO4_PIN        4
#define IDC_GPIO8_PIN        8
#define IDC_GPIO9_PIN        9
#define IDC_GPIO16_PIN      16
#define IDC_GPIO17_PIN      17
#define IDC_GPIO18_PIN      18
#define IDC_GPIO19_PIN      19

// SPI — via conector IDC (canal SPI0)
#define SPI_RX_PIN          16
#define SPI_CSN_PIN         17
#define SPI_SCK_PIN         18
#define SPI_TX_PIN          19
#define SPI_PORT            spi0

// Barra de terminais jacaré
// DIG0–DIG3 mapeados em GPIO0–GPIO3
// Também expõe GPIO28, GND analógico, GND, 3V3 e 5V
#define TERM_DIG0_PIN        0
#define TERM_DIG1_PIN        1
#define TERM_DIG2_PIN        2
#define TERM_DIG3_PIN        3


// Identificação do nó — sobrescrito em compile-time via -DNODE_ID=n
#ifndef NODE_ID
#define NODE_ID              0
#endif


#endif // HARDWARE_CONFIG_H
