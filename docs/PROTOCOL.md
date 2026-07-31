# The legacy miio UART protocol on zhimi.fan.za3

Everything below was obtained by talking to a real `zhimi.fan.za3` over the
serial bus and observing what the MCU answered. Where something could not be
verified, it says so.

## Framing

Both directions send plain ASCII lines terminated by a single `\r` at 115200
baud, 8N1. The **MCU is the master**: it polls the WiFi module and the module
answers. The module never sends unsolicited data.

## Messages from the MCU

| Message | Expected answer |
|---|---|
| `get_down` | `down <method> <args>` or `down none` |
| `props <key> <value> ...` | `ok` |
| `result "ok"` | `ok` (acknowledgement of a `down` command) |
| `error "<text>" <code>` | `ok` |
| `net` | `offline` / `unprov` / `uap` / `local` / `cloud` |
| `time` / `time posix` | formatted time or posix timestamp, `0` if unknown |
| `mac` | the MAC address |
| `model <model>` | `ok` |
| `mcu_version <version>` | `ok` |
| `restore` | `ok`, then factory reset |

Anything unknown should still be answered with `ok`, otherwise the MCU retries.

## Command format — the part that trips you up

Commands are queued and handed to the MCU as the answer to its next `get_down`:

```
MCU  -> get_down
ESP  -> down set_power "on"
MCU  -> result "ok"
MCU  -> props power "on"
```

**String arguments must be quoted, numeric arguments must not be.** This is the
single most important detail; getting it wrong looks like an unsupported method:

```
down set_power off        ->  error "invalid arg" -5001
down set_power 0          ->  error "invalid arg" -5001
down set_power ["off"]    ->  error "invalid arg" -5001
down set_power "off"      ->  ok
```

Error codes seen: `-5000 method not found`, `-5001 invalid arg`,
`-6007 device_busy`.

MIoT-style commands are rejected outright, which is what makes these devices
incompatible with MIoT components:

```
down get_properties 2 1 2 2 ...   ->  error "method not found" -5000
```

## Command set

| Command | Argument | Notes |
|---|---|---|
| `set_power` | `"on"` / `"off"` | |
| `set_speed_level` | `1..100` | stepless; **implicitly turns natural wind off** |
| `set_natural_level` | `0..100` | `0` = straight wind, `>0` = natural wind at that level |
| `set_fan_level` | `1..4` | gear; acts on whichever level is currently active |
| `set_angle_enable` | `"on"` / `"off"` | oscillation |
| `set_angle` | `30` / `60` / `90` / `120` | also enables oscillation |
| `set_move` | `"left"` / `"right"` | only while oscillation is off, else `-6007 device_busy` |
| `set_child_lock` | `"on"` / `"off"` | |
| `set_buzzer` | `0` / `1` | numeric, `"on"` gives `-5001` |
| `set_led_b` | `0` / `1` / `2` | bright / dim / off |
| `set_poweroff_time` | seconds | unit assumed, not verified |

There is no working read command. `get_prop <name>` is accepted but never
answers, `get_props` gives `-5000`.

## Wind mode state machine

Natural wind is active exactly while `natural_level > 0`. `speed_level` keeps
the straight wind level independently.

```
set_natural_level 40   ->  natural wind at 40
set_speed_level 60     ->  straight wind at 60, natural_level silently becomes 0
set_natural_level 0    ->  straight wind at the previous speed_level
```

**Both implicit transitions are not reported back.** After `set_speed_level`
the MCU echoes only `props speed_level 60 fan_level 3` — no `natural_level 0`.
A cached copy of the state has to be corrected locally or a "natural wind"
switch gets stuck on. This component does that in `set_straight_level()` and
`set_natural_level()`.

## What is reported, and when

The periodic/refresh report contains only five properties:

```
props power "on" angle_enable "off" fan_level 3 natural_level 0 speed_level 60
```

`angle`, `poweroff_time`, `child_lock`, `buzzer` and `led_b` are only ever sent
as an echo to a matching `set_*`, so their state is unknown after a reboot.

## Forcing a full report

The MCU dumps all properties whenever the reported network state *changes*.
Sending the same value twice does nothing, so a refresh is a transition:

```
down MIIO_net_change local
down MIIO_net_change cloud
```

The dump arrives roughly 6 seconds later.

## Physical buttons

The MCU announces key presses as a property:

```
props button_pressed "speed" speed_level 35 fan_level 2
props button_pressed "speed" power "off"
props button_pressed "angle" angle_enable "on" angle 90
```

Two keys exist on this model, `speed` and `angle`.

**A long press cannot be distinguished from a short one.** The message is
identical and no duration is transmitted. On the `speed` key a long press is
the power toggle, which is only recognisable indirectly by the accompanying
`power` change. On the `angle` key a long press does exactly what a short one
does.

What *is* usable is timing between events. Measured intervals for a deliberate
double press were around 200-230 ms, so a window of 900 ms is comfortable. A
double press on the `angle` key is particularly convenient because the two
oscillation toggles cancel out, leaving the swing state untouched — this
component uses it to toggle the wind mode without any network involved.
