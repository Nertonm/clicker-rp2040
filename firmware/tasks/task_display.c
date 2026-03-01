#include "task_display.h"
#include "FreeRTOS.h"
#include "task.h"
#include "config/firmware_config.h"
#include "drivers/display/display.h"
#include "drivers/display/ws2812.h"
#include "hardware_config.h"
#include "middleware/shared_state.h"
#include "pico/stdlib.h"
#include <stdio.h>

static void wheel_color(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b) {
  uint8_t p = 255 - pos;
  if (p < 85) {
    *r = 255 - p * 3;
    *g = 0;
    *b = p * 3;
  } else if (p < 170) {
    p -= 85;
    *r = 0;
    *g = p * 3;
    *b = 255 - p * 3;
  } else {
    p -= 170;
    *r = p * 3;
    *g = 255 - p * 3;
    *b = 0;
  }
}

static const char *status_to_text(connection_status_t status) {
  switch (status) {
  case STATUS_ONLINE:
    return "ONLINE";
  case STATUS_OFFLINE:
    return "OFFLINE";
  case STATUS_CONNECTING:
    return "CONNECTING";
  case STATUS_SYNCING:
    return "SYNCING";
  default:
    return "UNKNOWN";
  }
}

void task_display(void *param) {
  (void)param;

  char row1[24];
  char row2[24];
  char row3[24];
  uint8_t rainbow_phase = 0;
  uint8_t milestone_glow_ticks = 0;

  while (1) {
    uint32_t local = shared_state_get_local_score();
    uint32_t global = shared_state_get_global_score();
    connection_status_t status = shared_state_get_connection_status();
    bool led_flash = shared_state_take_led_flash_requested();
    bool milestone = shared_state_take_milestone_triggered();
    bool turbo_active = shared_state_get_turbo_active();

    if (turbo_active) {
      uint32_t now_ms = to_ms_since_boot(get_absolute_time());
      uint32_t until_ms = shared_state_get_turbo_until_ms();
      if ((int32_t)(until_ms - now_ms) <= 0) {
        shared_state_set_turbo_active(false);
        turbo_active = false;
      }
    }

    if (milestone) {
      milestone_glow_ticks = MILESTONE_GLOW_TICKS;
      for (int i = 0; i < 2; i++) {
        led_matrix_set_all(24, 16, 0);
        vTaskDelay(pdMS_TO_TICKS(80));
        led_clear_all();
        vTaskDelay(pdMS_TO_TICKS(50));
      }
    }

    snprintf(row1, sizeof(row1), "Node %d: %lu", NODE_ID, (unsigned long)local);
    snprintf(row2, sizeof(row2), "Global: %lu", (unsigned long)global);
    if (turbo_active) {
      snprintf(row3, sizeof(row3), "TURBO x3");
    } else {
      snprintf(row3, sizeof(row3), "%s", status_to_text(status));
    }

    display_clear();
    display_text(0, 0, "Cookie Clicker");
    display_text(2, 0, row1);
    display_text(4, 0, row2);
    display_text(6, 0, row3);
    display_show();

    uint8_t digit = (uint8_t)(local % 10u);
    if (milestone_glow_ticks > 0) {
      led_matrix_draw_number(digit, 20, 14, 0);
      milestone_glow_ticks--;
    } else if (turbo_active) {
      uint8_t r, g, b;
      wheel_color((uint8_t)(rainbow_phase + (digit * 12u)), &r, &g, &b);
      led_matrix_draw_number(digit, (uint8_t)(r / 18 + 2),
                             (uint8_t)(g / 18 + 2), (uint8_t)(b / 18 + 2));
      rainbow_phase += 7;
    } else {
      led_matrix_draw_number(digit, 8, 8, 8);
    }

    if (led_flash) {
      led_set(12, 0, 0, 15);
      vTaskDelay(pdMS_TO_TICKS(50));
      if (milestone_glow_ticks > 0) {
        led_matrix_draw_number(digit, 20, 14, 0);
      } else if (turbo_active) {
        uint8_t r, g, b;
        wheel_color((uint8_t)(rainbow_phase + (digit * 12u)), &r, &g, &b);
        led_matrix_draw_number(digit, (uint8_t)(r / 18 + 2),
                               (uint8_t)(g / 18 + 2), (uint8_t)(b / 18 + 2));
      } else {
        led_matrix_draw_number(digit, 8, 8, 8);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
  }
}
