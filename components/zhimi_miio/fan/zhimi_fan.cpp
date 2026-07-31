#include "zhimi_fan.h"
#include "esphome/core/log.h"

namespace esphome {
namespace zhimi_miio {

static const char *const TAG = "zhimi_miio.fan";

void ZhimiFan::setup() { this->parent_->register_props_listener(this); }

void ZhimiFan::dump_config() { LOG_FAN("", "Zhimi Fan", this); }

void ZhimiFan::control(const fan::FanCall &call) {
  if (call.get_state().has_value() && *call.get_state() != this->state)
    this->parent_->set_string_prop("power", *call.get_state() ? "on" : "off");

  if (call.get_oscillating().has_value() && *call.get_oscillating() != this->oscillating)
    this->parent_->set_string_prop("angle_enable", *call.get_oscillating() ? "on" : "off");

  if (call.get_speed().has_value()) {
    int speed = clamp(*call.get_speed(), 1, 100);
    // While natural wind is running the level lives in natural_level; writing
    // speed_level would silently drop the device back to straight wind.
    bool natural = this->parent_->get_string("mode") == "natural";
    this->parent_->set_number_prop(natural ? "natural_level" : "speed_level", speed);
  }

  // The MCU echoes every accepted change back as a props message, which is what
  // actually updates this entity. Publishing here only keeps the UI snappy.
  if (call.get_state().has_value())
    this->state = *call.get_state();
  if (call.get_oscillating().has_value())
    this->oscillating = *call.get_oscillating();
  if (call.get_speed().has_value())
    this->speed = clamp(*call.get_speed(), 1, 100);
  this->publish_state();
}

void ZhimiFan::on_props_update(ZhimiMiio *hub) {
  bool state = hub->get_bool("power", this->state);
  bool oscillating = hub->get_bool("angle_enable", this->oscillating);
  bool natural = hub->get_string("mode") == "natural";
  int speed = hub->get_number(natural ? "natural_level" : "speed_level", this->speed);
  speed = clamp(speed, 1, 100);

  if (state == this->state && oscillating == this->oscillating && speed == this->speed)
    return;

  ESP_LOGD(TAG, "MCU state: power=%s speed=%d oscillating=%s", ONOFF(state), speed, ONOFF(oscillating));
  this->state = state;
  this->oscillating = oscillating;
  this->speed = speed;
  this->publish_state();
}

}  // namespace zhimi_miio
}  // namespace esphome
