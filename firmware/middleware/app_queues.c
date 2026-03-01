#include "app_queues.h"
#include "config/firmware_config.h"

static QueueHandle_t queue_clicks;

void app_queues_init(void) {
  queue_clicks = xQueueCreate(CLICK_QUEUE_LEN, sizeof(click_msg_t));
  configASSERT(queue_clicks != NULL);
}

QueueHandle_t app_queues_get_clicks(void) { return queue_clicks; }
