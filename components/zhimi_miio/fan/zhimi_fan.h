#pragma once

#include "esphome/components/fan/fan.h"
#include "esphome/core/component.h"

#include "../zhimi_miio.h"

namespace esphome {
namespace zhimi_miio {

/* Fan entity for legacy zhimi fans.
 *
 * power        -> set_power "on"/"off"
 * speed 1..100 -> set_speed_level N   (straight wind)
 *              -> set_natural_level N (while natural wind is active)
 * oscillating  -> set_angle_enable "on"/"off"
 *
 * The device reports natural_level > 0 exactly while natural wind is active,
 * and set_speed_level implicitly clears it.
 */
class ZhimiFan : public Component, public fan::Fan, public ZhimiPropsListener {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_parent(ZhimiMiio *parent) { this->parent_ = parent; }

  fan::FanTraits get_traits() override {
    this->wire_preset_modes_(this->traits_);
    return this->traits_;
  }

  void on_props_update(ZhimiMiio *hub) override;

 protected:
  void control(const fan::FanCall &call) override;

  ZhimiMiio *parent_{nullptr};
  fan::FanTraits traits_{true, true, false, 100};
};

}  // namespace zhimi_miio
}  // namespace esphome
