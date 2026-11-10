#include "device/status_led.hpp"

#include <driver/gpio.h>
#include <nvs.h>

#include "core/status_led.hpp"
#include "device/board.hpp"
#include "device/network.hpp"

namespace device {

void StatusLed::start(bool enabled) {
  enabled_ = enabled;
  gpio_reset_pin(board::kStatusLed);
  gpio_set_direction(board::kStatusLed, GPIO_MODE_OUTPUT);
  gpio_set_level(board::kStatusLed, 1);  // off (active low)
  const esp_timer_create_args_t args = {
      .callback = on_tick,
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "status_led",
      .skip_unhandled_events = true,
  };
  if (esp_timer_create(&args, &timer_) == ESP_OK) esp_timer_start_periodic(timer_, 125'000);
}

void StatusLed::set_enabled(bool enabled) {
  enabled_ = enabled;
  save_enabled(enabled);
}

void StatusLed::on_tick(void* arg) {
  auto* self = static_cast<StatusLed*>(arg);
  bool on = false;
  if (self->enabled_) {
    const core::LedPattern pattern =
        core::status_pattern(self->network_.connected(), self->network_.setup_ap_active(),
                             self->service_.status().state);
    on = core::led_on(pattern, self->tick_++);
  }
  gpio_set_level(board::kStatusLed, on ? 0 : 1);
}

bool StatusLed::load_enabled() {
  nvs_handle_t h;
  uint8_t value = 1;
  if (nvs_open("device", NVS_READONLY, &h) == ESP_OK) {
    nvs_get_u8(h, "led", &value);
    nvs_close(h);
  }
  return value != 0;
}

void StatusLed::save_enabled(bool enabled) {
  nvs_handle_t h;
  if (nvs_open("device", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u8(h, "led", enabled ? 1 : 0);
  nvs_commit(h);
  nvs_close(h);
}

}  // namespace device
