#include "game_events.h"
#include "audio/buzzer.h"
#include "middleware/shared_state.h"
#include "pico/stdlib.h"

void trigger_milestone_feedback(void) {
  static uint32_t last_trigger_ms = 0;
  uint32_t now_ms = to_ms_since_boot(get_absolute_time());

  if ((int32_t)(now_ms - last_trigger_ms) < 250) {
    return;
  }

  last_trigger_ms = now_ms;
  shared_state_set_milestone_triggered(true);
  buzzer_tone(1800, 120);
}
