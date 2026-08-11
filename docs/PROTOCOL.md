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
| `set_mode` | `"natural"` / `"normal"` | carries the current level over; `"straight"` gives `-5001` |
| `set_speed_level` | `1..100` | stepless; **implicitly turns natural wind off** |
| `set_natural_level` | `0..100` | `0` = normal wind, `>0` = natural wind at that level |
| `set_fan_level` | `1..4` | gear; acts on whichever level is currently active |
| `set_angle_enable` | `"on"` / `"off"` | oscillation |
| `set_angle` | `30` / `60` / `90` / `120` | also enables oscillation |
| `set_move` | `"left"` / `"right"` | only while oscillation is off, else `-6007 device_busy` |
| `set_child_lock` | `"on"` / `"off"` | |
| `set_buzzer` | `0` / `1` | numeric, `"on"` gives `-5001` |
| `set_led_b` | `0` / `1` / `2` | bright / dim / off |
| `set_poweroff_time` | seconds | counts down; verified 600 → 499 in 105 s |

Every setter is rejected with `error "device_poweroff" -6011` while the device
is switched off, so `set_power "on"` has to come first.

## Reading properties

`get_prop` works, and it obeys the same quoting rule as the setters — which is
easy to miss, because the unquoted form does not fail, it just answers `null`:

```
down get_prop power                ->  result "null"
down get_prop "power"              ->  result "off"
down get_prop ["power"]            ->  result "null"
down get_prop                      ->  error "method not found" -5000
```

Multiple properties are read in a single command **comma separated**. Space
separation silently returns only the first value:

```
down get_prop "power" "speed_level"   ->  result "off"
down get_prop "power","speed_level"   ->  result "off",49
```

Values come back in the requested order, strings quoted and numbers bare.
Fourteen properties in one request worked without trouble:

```
down get_prop "power","mode","fan_level","speed_level","natural_level","angle",
              "angle_enable","child_lock","buzzer","led_b","poweroff_time",
              "ac_power","speed","use_time"
->  result "on","normal",2,33,0,90,"off","off",0,1,0,"on",451,175940
```

### Readable properties

| Property | Example | Notes |
|---|---|---|
| `power` | `"on"` | |
| `mode` | `"natural"` / `"normal"` | the authoritative wind mode |
| `fan_level` | `2` | gear 1–4 |
| `speed_level` | `33` | normal wind level |
| `natural_level` | `0` | natural wind level, `0` while in normal mode |
| `angle` | `90` | |
| `angle_enable` | `"off"` | |
| `child_lock` | `"off"` | |
| `buzzer` | `0` | |
| `led_b` | `1` | |
| `poweroff_time` | `0` | remaining seconds |
| `speed` | `451` | **measured fan RPM** |
| `use_time` | `175940` | total seconds of operation |
| `ac_power` | `"on"` | |
| `temp_dec`, `humidity`, `battery`, `bat_charge` | `"null"` | not present on this model |

Unsupported names answer `"null"` rather than an error, which makes probing for
a device's property set straightforward.

## Wind mode state machine

`mode` is the authoritative value, and `set_mode` is the clean way to switch —
it carries the current level over, so no level has to be written along with it:

```
set_mode "natural"   ->  natural wind, natural_level takes over speed_level
set_mode "normal"    ->  normal wind at speed_level, natural_level becomes 0
```

The level of whichever mode is active is written directly:

```
set_natural_level 40   ->  natural wind at 40
set_speed_level 60     ->  normal wind at 60, natural_level silently becomes 0
set_natural_level 0    ->  normal wind at the previous speed_level
```

**Mode changes are never echoed back as a `mode` property** — only the resulting
`natural_level` is. Anything caching the state either has to reflect the mode
change locally or re-read it, otherwise a "natural wind" switch gets stuck. This
component does both: `set_mode()` updates the cached value immediately and the
poll confirms it.

## What is reported, and when

The periodic/refresh report contains only five properties:

```
props power "on" angle_enable "off" fan_level 3 natural_level 0 speed_level 60
```

`mode`, `angle`, `poweroff_time`, `child_lock`, `buzzer`, `led_b`, `speed` and
`use_time` are never part of it — they have to be polled with `get_prop`.

## Forcing a full report

Polling with `get_prop` is the direct way, but there is also an indirect one:
the MCU dumps its five reported properties whenever the network state it was
told about *changes*. Sending the same value twice does nothing, so a refresh
is a transition:

```
down MIIO_net_change local
down MIIO_net_change cloud
```

The dump arrives roughly 6 seconds later. This is useful on devices where the
property names for `get_prop` are not known yet.

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

## The Bluetooth remote

The remote that ships with the fan does not talk to the MCU at all. It was
handled by the stock firmware **on the ESP32 WiFi module**, which is why it
stops working the moment ESPHome is flashed — nothing on the UART side ever
sees it.

It can be brought back without touching the pairing, because it is a plain
MiBeacon broadcaster and its payload is not encrypted.

### What it sends

BLE advertisements with service data under UUID `0xFE95`, advertised name
`ZHIMI-FAN`:

| Offset | Field |
|---|---|
| `0..1` | frame control, little endian |
| `2..3` | product id, little endian (`0x0423` on this remote) |
| `4` | frame counter |
| `5..10` | MAC address, reversed (present when frame control bit 4 is set) |
| then | object id (LE) + length + data |

Relevant frame control bits: `0x0001` factory new, `0x0008` **encrypted**,
`0x0010` MAC embedded, `0x0020` capability byte follows, `0x0040` carries an
event. The observed value is `0x2051` — factory new, MAC, event, version 2, and
crucially **the encryption bit is clear**.

The frame counter increments once per key press. A single press is broadcast
several times with the same counter, so deduplicating on it is what turns the
radio traffic into discrete events — and it rejects replayed frames for free.

### Objects

| Object | Meaning |
|---|---|
| `0x0002` | pairing request, payload `01 10` = "pair object `0x1001`" |
| `0x1001` | key press, three data bytes `<key> 00 <value>` |

An unpaired remote sends the pairing request for 30 s in 2 s intervals and then
goes quiet. **This does not have to be answered.** Pairing exists to register
the device in Mi Home and to hand out a bind key for encrypted payloads; with
the encryption bit clear there is nothing to decrypt, and a paired remote might
well start encrypting, which would make things worse.

### Key map

The remote has two keys and keeps its own gear counter, so a short press on the
top key transmits the *absolute* gear it has counted up to, not an increment:

| Data | Key | Meaning |
|---|---|---|
| `11 00 11` … `11 00 14` | top | short press, gear 1–4 |
| `11 00 02` | top | long press, power |
| `12 00 00` | bottom | short press, oscillation |
| `12 00 02` | bottom | long press, wind mode |

Note that a long press is a distinct value here (`0x02`), unlike the physical
keys on the fan itself where it cannot be told apart at all.

### Range

The module's antenna sits inside the fan's base. Close to the device (RSSI −46)
reception was gap free over dozens of presses; at −97 frames were dropped and
counter values went missing. Expect same-room range, not whole-flat range.

### No way back

The remote transmits only while a key is held and sleeps in between — there is
no window in which it listens. Feeding state back to it, for example to light an
LED for the current wind mode, is not possible.
