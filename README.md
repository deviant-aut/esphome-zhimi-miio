# esphome-zhimi-miio

ESPHome component for Xiaomi/Smartmi devices that speak the **legacy miio UART
protocol** instead of MIoT — the ones tagged `miio2miot` on
[Xiaomi MIoT Spec](https://home.miot-spec.com/), where the serial bus shows
messages like `props power "on"`.

## Tested devices

| Device | Model | Status | UART pins | Notes |
|---|---|---|---|---|
| Smartmi Standing Fan 2 | [`zhimi.fan.za3`](https://home.miot-spec.com/spec/zhimi.fan.za3) | ✅ verified on hardware | `TX GPIO17` / `RX GPIO16` | Full feature set. Wiring and teardown photos in [dhewg/esphome-miot#85](https://github.com/dhewg/esphome-miot/issues/85). |

Other `miio2miot` devices should work with the hub — the transport is the same
for all of them — but the command set differs per model and none have been
tested. If you get one running, a PR adding it to this table is welcome.

## Hardware

The WiFi module in these devices **already is an ESP32** — it is not replaced,
it gets flashed with ESPHome. The module talks to the fan's own MCU over UART,
and that MCU is what speaks the protocol described here; it keeps running the
Xiaomi firmware and handles the physical keys on its own.

Flashing needs a serial adapter soldered to the module's pads. **Unless you dump
the stock firmware first, this is a one-way trip** — which is what makes a
working component necessary rather than optional.

For the `zhimi.fan.za3`, the module pinout, soldering photos and the UART pins
are documented in the upstream issue:

- [Teardown and soldering photos](https://github.com/dhewg/esphome-miot/issues/85#issuecomment-3383123413)
- [UART pins and baud rate](https://github.com/dhewg/esphome-miot/issues/85#issuecomment-3383231641)

The board config needs these options, because Xiaomi's ESP32 modules do not
carry a valid eFuse MAC CRC:

```yaml
esp32:
  board: esp32dev
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_FREERTOS_UNICORE: y
    advanced:
      ignore_efuse_custom_mac: true
      ignore_efuse_mac_crc: true
```

## Why this exists

[dhewg/esphome-miot](https://github.com/dhewg/esphome-miot) is the component to
use for Xiaomi devices — but it speaks MIoT (`set_properties <siid> <piid>
<value>`). Devices like the `zhimi.fan.za3` answer every MIoT command with
`error "method not found" -5000`, which its README documents as unsupported.

Once the module has been flashed with ESPHome, going back to the stock firmware
is not usually an option, so the remaining path is to speak the old protocol.
That is what this component does.

The protocol was reverse engineered on the device; the full write-up is in
[docs/PROTOCOL.md](docs/PROTOCOL.md).

## Credits

The transport layer of this component is **closely modelled on
[`miot.cpp`](https://github.com/dhewg/esphome-miot/blob/main/components/miot/miot.cpp)
by [@dhewg](https://github.com/dhewg)** — the RX buffering in `loop()`, the
`process_message_()` dispatch, the network status handling and the `get_down`
command queue all follow its structure. This is a derivative work, not an
independent reimplementation, and it is published under the same license.

Everything above the transport is different: name-based properties instead of
`siid`/`piid`, legacy method names, the quoted-string argument format, the
`props` parser, physical button events and the fan platform.

## What is different from MIoT

| | MIoT (`dhewg/esphome-miot`) | legacy miio (this) |
|---|---|---|
| Addressing | numeric `siid` / `piid` | property names as plain text |
| Write | `set_properties 2 1 1` | `set_power "on"` |
| Read | `get_properties ...`, polled | none — the MCU pushes `props` |
| Arguments | positional numbers | strings quoted, numbers bare |

## Installation

Copy `components/zhimi_miio/` next to your ESPHome YAML and reference it:

```yaml
external_components:
  - source:
      type: local
      path: components
```

Or pull it straight from GitHub:

```yaml
external_components:
  - source: github://deviant-aut/esphome-zhimi-miio@main
```

Then see [`zhimi.fan.za3.yaml`](zhimi.fan.za3.yaml) for a complete
configuration, and copy `secrets.yaml.example` to `secrets.yaml`.

> **Check your UART pins** against the [tested devices](#tested-devices) table
> and the wiring photos linked under [Hardware](#hardware). Also set
> `logger: baud_rate: 0` — the MCU owns UART0 and logging to it corrupts the
> protocol.

## Configuration

### `zhimi_miio` hub

```yaml
uart:
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 115200

zhimi_miio:
  id: fan_hub
  on_button_press:
    - lambda: 'ESP_LOGI("app", "physical key: %s", x.c_str());'
```

| Option | Description |
|---|---|
| `on_button_press` | Fires when the MCU reports a physical key press. `x` is the key name, e.g. `"speed"` or `"angle"`. |
| `ota_net_indicator` | Network state reported to the MCU during an OTA update. Default `updating`. |

Methods available in lambdas:

| Method | Description |
|---|---|
| `queue_command(cmd)` | Send a raw command, e.g. `set_angle 120`. |
| `set_string_prop(prop, value)` | `set_<prop> "<value>"` |
| `set_number_prop(prop, value)` | `set_<prop> <value>` |
| `set_straight_level(n)` | Straight wind at `n`, keeps the cached state consistent. |
| `set_natural_level(n)` | Natural wind at `n`, `0` for straight wind. |
| `request_refresh()` | Make the MCU re-announce all properties. |
| `has_prop(name)` / `get_string(name)` / `get_number(name)` / `get_bool(name)` | Read cached state. |
| `props_summary()` | All known properties, for a diagnostic text sensor. |

Use `set_straight_level()` / `set_natural_level()` rather than writing
`speed_level` / `natural_level` directly — the MCU does not report the implicit
mode switch, so the cache has to be corrected locally.

### `fan` platform

```yaml
fan:
  - platform: zhimi_miio
    name: "Fan"
```

On/off, stepless speed 1–100 and oscillation. While natural wind is active the
speed is written to `natural_level`, otherwise to `speed_level`.

Everything else (natural wind, gear 1–4, oscillation angle, child lock, buzzer,
LED brightness, off delay) is done with standard `template` entities — see the
example YAML.

## Switching wind mode without a network

The MCU reports physical key presses, so multi-press detection can run entirely
on the ESP. The example config toggles natural/straight wind on a **double press
of the oscillation key**, which works with Home Assistant offline or WiFi down.

A long press cannot be used: the MCU sends the same message for short and long
presses. See [docs/PROTOCOL.md](docs/PROTOCOL.md#physical-buttons).

## Status

Verified on `zhimi.fan.za3` hardware (see [Tested devices](#tested-devices)):
power, stepless speed, gear 1–4, natural wind, oscillation and angle, physical
key events, and the double-press toggle.

Known gaps:

- `set_poweroff_time` is assumed to take **seconds**. The MCU does not report
  the value back on a refresh, so no countdown could be observed to confirm it.
- `child_lock`, `buzzer`, `led_b` and `angle` are never part of the periodic
  report, so those entities are optimistic or unknown until first set.
- The AP fallback path is reasoned from the ESPHome sources, not measured — the
  test would have meant flashing a config with an unreachable SSID.

## License

[ESPHome License](LICENSE), matching the upstream project this is derived from:
GPLv3 for the C++ sources, MIT for the Python parts.
