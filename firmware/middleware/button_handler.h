#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <stdint.h>

// Limiar de debounce configurável (microssegundos)
#define DEBOUNCE_US 50000    // 50ms
#define LONG_PRESS_US 800000 // 800ms distingue press longo do botão B

void button_handler_init(void);
uint32_t button_handler_get_a_count(void);
uint32_t button_handler_get_b_count(void);

#endif // BUTTON_HANDLER_H
