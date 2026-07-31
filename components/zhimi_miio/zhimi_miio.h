#pragma once

#include <cinttypes>
#include <deque>
#include <map>
#include <queue>
#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"

#ifdef USE_OTA_STATE_LISTENER
#include "esphome/components/ota/ota_backend.h"
#endif

namespace esphome {
namespace zhimi_miio {

/* Legacy Xiaomi miio UART protocol ("miio2miot" devices, e.g. zhimi.fan.za3).
 *
 * The MCU is the master and polls the WiFi module with "get_down"; the module
 * answers with "down <method> <args>" or "down none". Property changes are
 * pushed by the MCU as: props power "on" fan_level 1 speed_level 40
 * String arguments are quoted, numeric arguments are bare.
 */

class ZhimiMiio;

/// Anything that wants to be notified after a props message was parsed.
class ZhimiPropsListener {
 public:
  virtual void on_props_update(ZhimiMiio *hub) = 0;
};

class ZhimiMiio : public Component,
#ifdef USE_OTA_STATE_LISTENER
                  public ota::OTAGlobalStateListener,
#endif
                  public uart::UARTDevice {
 public:
  float get_setup_priority() const override { return setup_priority::DATA; }
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_ota_net_indicator(const std::string &indicator) { this->ota_net_indicator_ = indicator; }
  void register_props_listener(ZhimiPropsListener *listener) { this->listeners_.push_back(listener); }
  /// Fired when the MCU reports a press of a physical button ("speed", "angle", ...).
  void add_button_press_callback(std::function<void(const std::string &)> &&callback) {
    this->button_press_callback_.add(std::move(callback));
  }

  /// Enqueue a raw command, sent verbatim as "down <cmd>" on the next MCU poll.
  void queue_command(const std::string &cmd);
  /// set_<prop> "<value>"  (quoted, for string properties like power/angle_enable)
  void set_string_prop(const std::string &prop, const std::string &value);
  /// set_<prop> <value>  (bare, for numeric properties like speed_level/angle)
  void set_number_prop(const std::string &prop, int value);

  /* Wind mode, "natural" or "normal". set_mode carries the current level over,
   * so no level has to be written along with it. The MCU does not echo the mode
   * itself back (only the resulting natural_level), hence the local update.
   */
  void set_mode(const std::string &mode);

  /// Poll the MCU for the configured properties right now.
  void request_refresh();
  void set_poll_interval(uint32_t interval) { this->poll_interval_ = interval; }
  void set_poll_properties(const std::vector<std::string> &properties) { this->poll_properties_ = properties; }

  bool has_prop(const std::string &name) const { return this->props_.count(name) > 0; }
  std::string get_string(const std::string &name, const std::string &default_value = "") const;
  int get_number(const std::string &name, int default_value = 0) const;
  bool get_bool(const std::string &name, bool default_value = false) const;
  /// All known properties as "key=value key=value", for the diagnostic text sensor.
  std::string props_summary() const;

  const std::string &get_model() const { return this->model_; }
  const std::string &get_mcu_version() const { return this->mcu_version_; }

 protected:
#ifdef USE_OTA_STATE_LISTENER
  void on_ota_global_state(ota::OTAState state, float progress, uint8_t error, ota::OTAComponent *comp) override;
#endif

  const char *get_net_reply_();
  std::string get_time_reply_(bool posix);
  void queue_net_change_command_(bool force);
  void send_reply_(const char *reply);
  void process_message_();
  void parse_props_(char *p);
  void parse_get_prop_result_(char *p);
  void set_local_prop_(const std::string &name, const std::string &value);
  void notify_listeners_();
  void poll_();
  std::string get_printable_rx_message_();

  static const size_t MAX_LINE_LENGTH = 512;

  uint8_t rx_message_[MAX_LINE_LENGTH + 1];
  size_t rx_count_{0};
  uint32_t last_rx_char_timestamp_{0};
  const char *last_net_reply_{nullptr};
  bool props_dirty_{false};
  bool expect_get_prop_result_{false};
  bool poll_pending_{false};
  uint32_t poll_interval_{0};
  std::vector<std::string> poll_properties_;
  std::string poll_command_;
  std::string pending_button_;
  CallbackManager<void(const std::string &)> button_press_callback_;
  std::string model_;
  std::string mcu_version_;
  std::string ota_net_indicator_;
  std::map<std::string, std::string> props_;
  std::queue<std::string> command_queue_;
  std::vector<ZhimiPropsListener *> listeners_;
};

class ButtonPressTrigger : public Trigger<std::string> {
 public:
  explicit ButtonPressTrigger(ZhimiMiio *parent) {
    parent->add_button_press_callback([this](const std::string &button) { this->trigger(button); });
  }
};

}  // namespace zhimi_miio
}  // namespace esphome
