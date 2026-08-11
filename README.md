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
| Read | `get_properties <siid> <piid> ...` | `get_prop "name","name",...` |
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
  poll_interval: 10s
  poll_properties: [power, mode, speed_level, natural_level, angle]
  on_button_press:
    - lambda: 'ESP_LOGI("app", "physical key: %s", x.c_str());'
```

Polling is what makes the state real. The MCU pushes only five properties on
its own; everything else — including the wind mode itself — has to be read back
with `get_prop`. Probing a new device is easy because unsupported property
names answer `"null"` instead of failing.

| Option | Description |
|---|---|
| `poll_properties` | Property names to read back with `get_prop`. Empty (default) disables polling. |
| `poll_interval` | How often to poll. Default `10s`. |
| `on_button_press` | Fires when the MCU reports a physical key press. `x` is the key name, e.g. `"speed"` or `"angle"`. |
| `ota_net_indicator` | Network state reported to the MCU during an OTA update. Default `updating`. |

Methods available in lambdas:

| Method | Description |
|---|---|
| `queue_command(cmd)` | Send a raw command, e.g. `set_angle 120`. |
| `set_string_prop(prop, value)` | `set_<prop> "<value>"` |
| `set_number_prop(prop, value)` | `set_<prop> <value>` |
| `set_mode(mode)` | Wind mode, `"natural"` or `"normal"`. Keeps the cached value in sync. |
| `request_refresh()` | Poll all configured properties right now. |
| `has_prop(name)` / `get_string(name)` / `get_number(name)` / `get_bool(name)` | Read cached state. |
| `props_summary()` | All known properties, for a diagnostic text sensor. |

Use `set_mode()` rather than writing `natural_level` directly — it carries the
current level over, and the MCU never echoes the mode back, so the cached value
has to be updated locally.

### `fan` platform

```yaml
fan:
  - platform: zhimi_miio
    name: "Fan"
```

On/off, stepless speed 1–100 and oscillation. While natural wind is active the
speed is written to `natural_level`, otherwise to `speed_level`.

Everything else (natural wind, gear 1–4, oscillation angle, child lock, buzzer,
LED brightness, off delay, fan RPM, operating hours) is done with standard
`template` entities reading from the polled state — see the example YAML.

## Bringing the Bluetooth remote back

The remote that comes with the fan **stops working when you flash ESPHome**, and
this catches people out: the MCU knows nothing about it, so nothing on the UART
side explains where it went. It was handled by the stock firmware on the ESP32
module you just overwrote.

You can have it back, and you do not need to redo the pairing. The remote is a
MiBeacon broadcaster and it sends its key presses **unencrypted**, so the ESP32
radio — the same one, now running ESPHome — only has to listen:

```yaml
esp32_ble_tracker:
  on_ble_service_data_advertise:
    - service_uuid: FE95
      mac_address: AA:BB:CC:DD:EE:FF
      then:
        - lambda: |-  # parse object 0x1001, then drive the hub
```

The complete decoder, including the frame counter check that both debounces the
repeated broadcasts and blocks replays, is in
[`zhimi.fan.za3.yaml`](zhimi.fan.za3.yaml). The frame layout, the key map and
how to find your own remote's address are in
[docs/PROTOCOL.md](docs/PROTOCOL.md#the-bluetooth-remote).

Two things worth knowing before you build on this:

- The remote keeps its **own** gear counter and transmits the absolute gear, so
  it drifts out of step with the fan whenever the speed is changed anywhere
  else. That is stock behaviour, not something this component introduces.
- There is **no way back to the remote**. It sleeps between key presses and
  never listens, so state cannot be pushed to it.

Enabling BLE costs roughly 600 KB of flash. It still fits comfortably on the
4 MB module — the full example config builds to 73 % of the app partition.

## Switching wind mode without a network

The MCU reports physical key presses, so multi-press detection can run entirely
on the ESP. The example config toggles between natural and normal wind on a
**double press of the oscillation key**, which works with Home Assistant offline
or WiFi down.

A long press cannot be used: the MCU sends the same message for short and long
presses. See [docs/PROTOCOL.md](docs/PROTOCOL.md#physical-buttons).

## Status

Verified on `zhimi.fan.za3` hardware (see [Tested devices](#tested-devices)):
power, stepless speed, gear 1–4, natural wind, oscillation and angle, physical
key events, the double-press toggle, and reading every property back including
fan RPM and operating hours.

The Bluetooth remote is verified as well — every key and every value in the
[key map](docs/PROTOCOL.md#key-map) was confirmed against a labelled press
sequence, the fan was then driven from the remote for 25 consecutive presses
without a misread, and the "switch on, then set the gear" sequence was checked
against the MCU's error replies to make sure the second command is not rejected.

Known gaps:

- The AP fallback path is reasoned from the ESPHome sources, not measured — the
  test would have meant flashing a config with an unreachable SSID.
- `get_prop` was tested with up to 14 properties in one request; where the
  actual limit is was not established.

## License

[ESPHome License](LICENSE), matching the upstream project this is derived from:
GPLv3 for the C++ sources, MIT for the Python parts.
