#include "device/factory_reset.hpp"

#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>

#include "device/board.hpp"

namespace device {
namespace {

constexpr const char* kTag = "reset";
constexpr int kHoldMs = 10'000;
constexpr int kReleaseWaitMs = 120'000;
constexpr int kPollMs = 100;

// NVS namespaces holding user configuration ("device" and "sys" are board facts and stay).
constexpr const char* kUserNamespaces[] = {"wifi", "connect", "camera", "web", "mqtt", "link"};

void status_led(bool on) { gpio_set_level(board::kStatusLed, on ? 0 : 1); }  // active low

bool pin_held() { return gpio_get_level(board::kResetPin) == 0; }

}  // namespace

void erase_user_config() {
  for (const char* ns : kUserNamespaces) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) continue;  // never written: nothing to erase
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
  }
  ESP_LOGW(kTag, "user configuration erased");
}

void check_factory_reset_pin(core::SystemControl& system) {
  // GPIO12 is a strapping pin (flash voltage), already latched by now; the pull-up is only
  // enabled for this check and the pin goes back to its reset default afterwards.
  gpio_set_direction(board::kResetPin, GPIO_MODE_INPUT);
  gpio_set_pull_mode(board::kResetPin, GPIO_PULLUP_ONLY);
  vTaskDelay(pdMS_TO_TICKS(20));
  const auto restore_pin = [] { gpio_set_pull_mode(board::kResetPin, GPIO_PULLDOWN_ONLY); };
  if (!pin_held()) {
    restore_pin();
    return;
  }

  gpio_set_direction(board::kStatusLed, GPIO_MODE_OUTPUT);
  ESP_LOGW(kTag, "GPIO12 held low: keep it for %d s to factory reset", kHoldMs / 1000);
  for (int t = 0; t < kHoldMs; t += kPollMs) {
    status_led((t / 250) % 2 == 0);
    if (!pin_held()) {
      ESP_LOGW(kTag, "released early: factory reset cancelled");
      status_led(false);
      restore_pin();
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }

  erase_user_config();
  status_led(true);
  ESP_LOGW(kTag, "remove the jumper to reboot");
  // Rebooting with the pin still low would only repeat the reset: wait for the release.
  for (int t = 0; t < kReleaseWaitMs && pin_held(); t += kPollMs) vTaskDelay(pdMS_TO_TICKS(kPollMs));
  status_led(false);
  restore_pin();
  system.reboot(core::RebootReason::ConfigReset);
}

}  // namespace device
