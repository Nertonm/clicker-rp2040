#include "FreeRTOS.h"
#include "task.h"

#include "middleware/app_queues.h"
#include "middleware/game_events.h"
#include "middleware/shared_state.h"
#include "task_buttons.h"

void task_buttons(void *param) {
  (void)param;

  QueueHandle_t queue_clicks = app_queues_get_clicks();

  while (1) {
    uint32_t pending = shared_state_take_pending_clicks();
    if (pending > 0) {
      shared_state_set_led_flash_requested(true);

      click_msg_t msg = {.clicks = pending};
      if (xQueueSend(queue_clicks, &msg, 0) == pdPASS) {
        uint32_t local_now = shared_state_get_local_score();
        uint32_t global_now = shared_state_get_global_score();
        uint32_t local_next = local_now + pending;

        shared_state_set_local_score(local_next);
        shared_state_set_global_score(global_now + pending);

        if ((local_now / 10u) < (local_next / 10u)) {
          trigger_milestone_feedback();
        }
      } else {
        shared_state_restore_clicks(pending);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
