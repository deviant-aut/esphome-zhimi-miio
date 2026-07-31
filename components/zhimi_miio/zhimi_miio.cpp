#include <cctype>
#include <cstring>

#include "zhimi_miio.h"
#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/core/string_ref.h"
#include "esphome/core/util.h"

#ifdef USE_CAPTIVE_PORTAL
#include "esphome/components/captive_portal/captive_portal.h"
#endif

#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

namespace esphome {
namespace zhimi_miio {

static const char *const TAG = "zhimi_miio";
static const int RECEIVE_TIMEOUT = 300;
static const char *const NET_OFFLINE = "offline";
static const char *const NET_UNPROV = "unprov";
static const char *const NET_UAP = "uap";
static const char *const NET_LOCAL = "local";
static const char *const NET_CLOUD = "cloud";

void ZhimiMiio::setup() {
  // discard whatever the MCU sent before we were listening
  uint8_t c;
  while (this->available())
    if (this->read_byte(&c))
      if (this->rx_count_ < MAX_LINE_LENGTH)
        this->rx_message_[this->rx_count_++] = c;

  if (this->rx_count_ > 0) {
    ESP_LOGD(TAG, "Discarding initial MCU data: %s", this->get_printable_rx_message_().c_str());
    this->rx_count_ = 0;
  }

  this->queue_net_change_command_(true);
  this->queue_command("MIIO_mcu_version_req");
}

#ifdef USE_OTA_STATE_LISTENER
void ZhimiMiio::on_ota_global_state(ota::OTAState state, float progress, uint8_t error, ota::OTAComponent *comp) {
  switch (state) {
    case ota::OTA_STARTED:
      // loop() stops running during an update, so bypass the queue
      this->send_reply_((std::string("down MIIO_net_change ") + this->ota_net_indicator_).c_str());
      break;
    case ota::OTA_ERROR:
      this->queue_net_change_command_(true);
      break;
    default:
      break;
  }
}
#endif

void ZhimiMiio::loop() {
  uint8_t c;

  if (this->rx_count_ > 0 && millis() - this->last_rx_char_timestamp_ > RECEIVE_TIMEOUT) {
    ESP_LOGE(TAG, "Timeout while receiving from MCU, dropping '%s'", this->get_printable_rx_message_().c_str());
    this->rx_count_ = 0;
  }

  while (this->available()) {
    if (!this->read_byte(&c))
      break;

    if (c == '\r') {
      ESP_LOGV(TAG, "Received MCU message '%s'", this->get_printable_rx_message_().c_str());
      this->process_message_();
      this->rx_count_ = 0;
      break;
    }

    if (this->rx_count_ >= MAX_LINE_LENGTH) {
      ESP_LOGE(TAG, "MCU message too long, dropping '%s'", this->get_printable_rx_message_().c_str());
      this->rx_count_ = 0;
    }

    this->rx_message_[this->rx_count_++] = c;
    this->last_rx_char_timestamp_ = millis();
  }

  if (this->props_dirty_) {
    this->props_dirty_ = false;
    this->notify_listeners_();
  }

  // fired after the entities have seen the accompanying property changes
  if (!this->pending_button_.empty()) {
    std::string button = std::move(this->pending_button_);
    this->pending_button_.clear();
    ESP_LOGD(TAG, "Physical button pressed: %s", button.c_str());
    this->button_press_callback_.call(button);
  }
}

void ZhimiMiio::dump_config() {
  ESP_LOGCONFIG(TAG, "Zhimi legacy miio:");
  if (!this->model_.empty())
    ESP_LOGCONFIG(TAG, "  Model: %s", this->model_.c_str());
  if (!this->mcu_version_.empty())
    ESP_LOGCONFIG(TAG, "  MCU Version: %s", this->mcu_version_.c_str());
  ESP_LOGCONFIG(TAG, "  OTA net indicator: %s", this->ota_net_indicator_.c_str());
  ESP_LOGCONFIG(TAG, "  Properties: %s", this->props_summary().c_str());
}

std::string ZhimiMiio::get_printable_rx_message_() {
  std::string s;
  s.reserve(this->rx_count_);
  for (size_t i = 0; i < this->rx_count_; ++i) {
    if (std::isprint(this->rx_message_[i])) {
      s += (char) this->rx_message_[i];
    } else {
      s += str_snprintf("\\x%02X", 4, this->rx_message_[i]);
    }
  }
  return s;
}

void ZhimiMiio::queue_command(const std::string &cmd) {
  ESP_LOGD(TAG, "Queuing MCU command '%s'", cmd.c_str());
  this->command_queue_.push(cmd);
}

void ZhimiMiio::set_string_prop(const std::string &prop, const std::string &value) {
  this->queue_command("set_" + prop + " \"" + value + "\"");
}

void ZhimiMiio::set_number_prop(const std::string &prop, int value) {
  this->queue_command("set_" + prop + " " + to_string(value));
}

void ZhimiMiio::set_local_prop_(const std::string &name, const std::string &value) {
  if (this->props_[name] == value)
    return;
  this->props_[name] = value;
  this->props_dirty_ = true;
}

void ZhimiMiio::set_straight_level(int level) {
  this->set_number_prop("speed_level", level);
  this->set_local_prop_("speed_level", to_string(level));
  this->set_local_prop_("natural_level", "0");
}

void ZhimiMiio::set_natural_level(int level) {
  this->set_number_prop("natural_level", level);
  this->set_local_prop_("natural_level", to_string(level));
}

void ZhimiMiio::request_refresh() {
  // The MCU dumps all properties whenever the reported network state changes.
  const char *net = this->get_net_reply_();
  this->queue_command(std::string("MIIO_net_change ") + (std::strcmp(net, NET_LOCAL) == 0 ? NET_CLOUD : NET_LOCAL));
  this->queue_command(std::string("MIIO_net_change ") + net);
}

std::string ZhimiMiio::get_string(const std::string &name, const std::string &default_value) const {
  auto it = this->props_.find(name);
  return it == this->props_.end() ? default_value : it->second;
}

int ZhimiMiio::get_number(const std::string &name, int default_value) const {
  auto it = this->props_.find(name);
  if (it == this->props_.end())
    return default_value;
  return parse_number<int>(it->second).value_or(default_value);
}

bool ZhimiMiio::get_bool(const std::string &name, bool default_value) const {
  auto it = this->props_.find(name);
  if (it == this->props_.end())
    return default_value;
  return it->second == "on" || it->second == "true" || it->second == "1";
}

std::string ZhimiMiio::props_summary() const {
  std::string s;
  for (auto it = this->props_.cbegin(); it != this->props_.cend(); ++it) {
    if (!s.empty())
      s += ' ';
    s += it->first + '=' + it->second;
  }
  return s;
}

void ZhimiMiio::notify_listeners_() {
  for (auto *listener : this->listeners_)
    listener->on_props_update(this);
}

const char *ZhimiMiio::get_net_reply_() {
  if (remote_is_connected())
    return NET_CLOUD;
  if (network::is_connected())
    return NET_LOCAL;
#ifdef USE_CAPTIVE_PORTAL
  if (captive_portal::global_captive_portal != nullptr && captive_portal::global_captive_portal->is_active())
    return NET_UAP;
#endif
  if (network::is_disabled())
    return NET_UNPROV;
  return NET_OFFLINE;
}

void ZhimiMiio::queue_net_change_command_(bool force) {
  const char *reply = this->get_net_reply_();
  if (!force && reply == this->last_net_reply_)
    return;
  ESP_LOGI(TAG, "Network status changed to '%s'", reply);
  this->queue_command(std::string("MIIO_net_change ") + reply);
  this->last_net_reply_ = reply;
}

std::string ZhimiMiio::get_time_reply_(bool posix) {
#ifdef USE_TIME
  auto now = ESPTime::from_epoch_local(::time(nullptr));

  if (!now.is_valid()) {
    // expected while running offline: the time source is Home Assistant
    ESP_LOGV(TAG, "MCU time request: no time source available");
    return "0";
  }

  if (posix)
    return to_string((int64_t) now.timestamp);

  return now.strftime("%Y-%m-%d %H:%M:%S");
#else
  return "0";
#endif
}

void ZhimiMiio::send_reply_(const char *reply) {
  ESP_LOGV(TAG, "Sending reply '%s' to MCU", reply);
  this->write_str(reply);
  this->write_byte('\r');
  this->flush();
}

// props <key> <value> ... where <value> is either bare (numeric) or "quoted"
void ZhimiMiio::parse_props_(char *p) {
  while (p != nullptr && *p != '\0') {
    while (*p == ' ')
      ++p;
    if (*p == '\0')
      break;

    char *key = p;
    while (*p != '\0' && *p != ' ')
      ++p;
    if (*p == '\0')
      break;  // key without value
    *p++ = '\0';

    while (*p == ' ')
      ++p;
    if (*p == '\0')
      break;

    char *value;
    if (*p == '"') {
      value = ++p;
      while (*p != '\0' && *p != '"')
        ++p;
    } else {
      value = p;
      while (*p != '\0' && *p != ' ')
        ++p;
    }
    if (*p != '\0')
      *p++ = '\0';

    ESP_LOGD(TAG, "Property %s = %s", key, value);
    this->props_[key] = value;
    this->props_dirty_ = true;

    // The MCU announces presses of the physical buttons ("speed", "angle") as a
    // property. Treat it as an event, not as state.
    if (std::strcmp(key, "button_pressed") == 0)
      this->pending_button_ = value;
  }
}

void ZhimiMiio::process_message_() {
  this->rx_message_[this->rx_count_] = 0;
  char *saveptr = nullptr;
  const char *cmd_str = strtok_r(reinterpret_cast<char *>(this->rx_message_), " ", &saveptr);
  if (cmd_str == nullptr)
    return;
  const StringRef cmd(cmd_str);

  this->queue_net_change_command_(false);

  if (cmd == "get_down") {
    if (this->command_queue_.empty()) {
      this->send_reply_("down none");
    } else {
      this->send_reply_((std::string("down ") + this->command_queue_.front()).c_str());
      this->command_queue_.pop();
    }
  } else if (cmd == "props") {
    this->parse_props_(saveptr);
    this->send_reply_("ok");
  } else if (cmd == "result") {
    // acknowledgement of a down command, e.g. result "ok"
    ESP_LOGV(TAG, "MCU result: %s", saveptr != nullptr ? saveptr : "");
    this->send_reply_("ok");
  } else if (cmd == "net") {
    this->send_reply_(this->get_net_reply_());
  } else if (cmd == "time") {
    const char *arg = strtok_r(nullptr, " ", &saveptr);
    bool posix = arg != nullptr && std::strcmp(arg, "posix") == 0;
    this->send_reply_(this->get_time_reply_(posix).c_str());
  } else if (cmd == "mac") {
    this->send_reply_(get_mac_address().c_str());
  } else if (cmd == "model") {
    const char *model = strtok_r(nullptr, " ", &saveptr);
    if (model != nullptr) {
      this->model_ = model;
      ESP_LOGI(TAG, "Model: %s", this->model_.c_str());
    }
    this->send_reply_("ok");
  } else if (cmd == "mcu_version") {
    const char *version = strtok_r(nullptr, " ", &saveptr);
    if (version != nullptr) {
      this->mcu_version_ = version;
      ESP_LOGI(TAG, "MCU Version: %s", this->mcu_version_.c_str());
    }
    this->send_reply_("ok");
  } else if (cmd == "error") {
    const char *error = strtok_r(nullptr, "\"", &saveptr);
    const char *code = error != nullptr ? strtok_r(nullptr, " ", &saveptr) : nullptr;
    ESP_LOGE(TAG, "MCU command error %s: %s", code != nullptr ? code : "", error != nullptr ? error : "");
    this->send_reply_("ok");
  } else if (cmd == "restore") {
    ESP_LOGI(TAG, "Resetting to factory defaults...");
    this->send_reply_("ok");
    global_preferences->reset();
    App.safe_reboot();
  } else {
    ESP_LOGW(TAG, "Unhandled MCU message '%s'", this->get_printable_rx_message_().c_str());
    this->send_reply_("ok");
  }
}

}  // namespace zhimi_miio
}  // namespace esphome
