# Dreo 712S

Notes for adding the Dreo 712S (fan + light + ambient RGB) to this component.
Recovered from one unit; treat the datapoint map as model-specific.

See [`example-dreo-712s-esp32c3.yaml`](../../example-dreo-712s-esp32c3.yaml) for a
full configuration.

The example pulls the component from `github://davidc/dreo-protocol`, like the
other examples in this repo:

```bash
esphome compile example-dreo-712s-esp32c3.yaml
```

It also needs the stock-compatible [`dreo_partitions.csv`](dreo_partitions.csv)
from this directory. The `partitions:` path in the YAML is resolved relative to
the config file, so keep the `docs/dreo-712s/` layout, or move the CSV next to
your config and change the path to match.

To build against a local checkout instead (for testing changes to the component
before they are merged upstream), swap the `external_components` block for:

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [dreo]
```

## Hardware

- WiFi module: ESP32-C3-WROOM-02U on a `CL187A_WIFI_V1.0` carrier board.
- Product MCU: SC95F8613B family 8051.
- MCU bridge UART: ESP `GPIO0` (TX) / `GPIO1` (RX), 115200 8N1.
- Ambient RGB: driven locally by the ESP on `GPIO7` (17 LEDs), **not** an MCU
  datapoint. Use a normal ESPHome light platform for it.

## Datapoint map

| DP | Stock key | Type | Notes |
|---:|---|---|---|
| 1 | `poweron` | bool | Overall latch/diagnostic. Not a safe fan-only state. |
| 3 | `fanon` | bool | Fan on/off. |
| 4 | `lighton` | bool | Main white/CCT light on/off. |
| 5 | `atmon` | bool | Ambient RGB enable. |
| 6 | `mode` | int/value | Direction/mode. **1 = forward, 4 = reverse** (not 0/1). |
| 7 | `windlevel` | int/value | Fan level. Remote steps map to 1,3,5,7,9,12; accepts 1..12. |
| 8 | `brightness` | int | Main light brightness, 0..100. |
| 9 | `colortemp` | int | Main light colour temp, 0..100, **higher = cooler**. |
| 19 | `muteon` | bool | Inverted beep: `true` = quiet/muted. |
| 21 | `timeroff.du` | int | Timer-off duration, minutes (1h = 60). |
| 24 | `temperature` | int | Temperature-like reading (read-only). |
| 25 | `rgbpresetsel` | int/value | Ambient RGB preset select. |
| 27 | `atmbri` | int | Ambient RGB brightness, 0..100. |

Other datapoints exist in the stock model: scene fields DP14-18, schedule DP22,
`predefine` DP28, and scene/effect/favorite string blobs at DP100-102. The
component can now parse string datapoints and exposes generic string setters for
template `text` entities.

## Protocol notes specific to the 712S

- **Command `0x09`** — an i4season-specific version-query helper sent during
  startup in addition to the standard `0x01` product query. Enable
  `query_version_on_sync: true` for the 712S.
- **Command `0x03`** — WiFi/network status. The stock 712S sends payload `00 01`
  after sync; enable `wifi_status_on_sync: true`.
- **Command `0x0E` (button events)** — remote/panel presses with payload
  `02 01 <button_code>`. The component exposes `on_button_event` and an optional
  `button_event_repeat_guard` for exact-frame deduplication:

  | Code | Button | Code | Button |
  |---:|---|---:|---|
  | `0x01`..`0x06` | fan speed 1..6 | `0x0a` | brightness up |
  | `0x07` | fan on/off | `0x0b` | reverse direction |
  | `0x08` | brightness down | `0x0d` | colour temp |
  | `0x09` | light on/off | `0x12` | RGB change |
  | `0x0f`/`0x10`/`0x11` | timer 1h/4h/8h | | |

  Some physical presses do not always emit a fresh datapoint report. The 712S
  example mirrors fan speed, fan on/off, light on/off, and direction button
  events into the local datapoint cache so Home Assistant state tracks the
  physical remote without echoing a command back to the MCU.

## 712S entity mapping

- **Fan** is a normal `fan` entity. DP1 is asserted before fan-on commands, DP3
  is on/off, DP7 is speed, and DP6 is direction. The `speed_mappings` option
  maps six HA speeds to stock levels `1,3,5,7,9,12`; `direction_forward_value: 1`
  and `direction_reverse_value: 4` preserve the stock direction values.
- **Main light** is a datapoint-backed `light` entity: DP4 on/off, DP8
  brightness, DP9 colour temperature. DP9 is inverted from Home Assistant mireds:
  higher raw values are cooler.
- **Ambient RGB** is a standard `esp32_rmt_led_strip` on GPIO7 with 17 LEDs. DP5,
  DP25, and DP27 are still mirrored so remote/app preset state remains coherent.
- **Button beep** is inverted: DP19 `true` means muted/quiet, so the example
  exposes a positive `Button Beep` switch.
