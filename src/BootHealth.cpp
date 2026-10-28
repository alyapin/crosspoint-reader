#include "BootHealth.h"

#include <Arduino.h>
#include <Logging.h>

#if !defined(SIMULATOR) && defined(CONFIG_APP_ROLLBACK_ENABLE)
#include <esp_ota_ops.h>
#define BOOTHEALTH_ROLLBACK_GUARD 1

// Overrides the Arduino core's weak default (false = confirm during init).
extern "C" bool verifyRollbackLater() { return true; }
#endif

namespace boothealth {

namespace {
bool done = false;
}  // namespace

bool confirmNow(const char* reason) {
  if (done) return true;
#ifdef BOOTHEALTH_ROLLBACK_GUARD
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK) {
    LOG_INF("BOOT", "Image state unavailable (no OTA data); rollback guard inactive");
    done = true;
    return true;
  }
  if (state != ESP_OTA_IMG_PENDING_VERIFY) {
    done = true;
    return true;
  }
  if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
    LOG_ERR("BOOT", "Failed to confirm firmware image (%s)", reason);
    return false;
  }
  LOG_INF("BOOT", "Firmware image confirmed (%s)", reason);
#else
  (void)reason;
#endif
  done = true;
  return true;
}

void confirmIfHealthy() {
  static unsigned long windowStartMs = 0;
  if (done) return;
  const unsigned long now = millis();
  if (windowStartMs == 0) windowStartMs = now != 0 ? now : 1;
  if (now - windowStartMs < HEALTHY_AFTER_MS) return;
  // On failure, wait another window instead of retrying (and logging) every loop.
  if (!confirmNow("healthy runtime")) windowStartMs = now != 0 ? now : 1;
}

}  // namespace boothealth
