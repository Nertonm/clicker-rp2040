#ifndef APP_QUEUES_H
#define APP_QUEUES_H

#include "FreeRTOS.h"
#include "queue.h"
#include <stdint.h>

typedef struct {
  uint32_t clicks;
} click_msg_t;

void app_queues_init(void);
QueueHandle_t app_queues_get_clicks(void);

#endif /* APP_QUEUES_H */
