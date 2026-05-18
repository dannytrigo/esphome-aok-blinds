# ESPHome A-OK Cover Component

A native ESPHome component for controlling A-OK 433 MHz tubular blind motors
(used by AC114, AC123, AM25 product families, sold under many brands).

This works as a **virtual remote**: you make up a 24-bit remote ID, pair each
motor to it, and ESPHome can then drive all your blinds without ever needing
to clone or capture a physical remote.

## Why this exists

A-OK motors use a tri-state OOK protocol on 433.92 MHz that:

- Sonoff RF Bridge stock firmware can't capture cleanly (Portisch sniffing
  doesn't yield consistent codes for these remotes).
- ESPHome's built-in protocols don't match (it's not Dooya, not RC-Switch).
- Naive raw-replay tends to drift just enough on the ~270/565 µs pulse
  widths that the motor's bit-slicer rejects the replay.

Generating the waveform from scratch with the correct ID, channel bitmask,
and checksum is much more reliable than capture-and-replay.

## Protocol

Frame is 65 bits, MSB first:

```
[0xA3 start : 8][remote ID : 24][channel : 16][cmd : 8][checksum : 8][1]
```

- **Checksum** = 8-bit sum of (ID bytes) + (channel bytes) + (cmd byte)
- **Commands**: UP=0x0B, DOWN=0x43, STOP=0x23, PROGRAM=0x53,
  AFTER=0x24 (sent ~200 ms after UP/DOWN), CHANGE_DIR=0x50

Timings (microseconds): AGC high 5300, AGC low 530, short pulse 270,
long pulse 565, inter-frame silence 5030. Each frame is repeated 8 times.

Credit: protocol reverse-engineered by Jason von Nieda and documented by
Antti Kirjavainen at <https://github.com/akirjavainen/A-OK>.

## Hardware

Tested target: **Sonoff RF Bridge v2 with the "direct hack" mod** (cut 4
PCB traces and add 2 resistors to bypass the EFM8BB1 / OB38S003 chip so
the ESP8266 directly drives the SYN115 transmitter).

After the mod, the transmitter sits on GPIO5 on most boards — but some
R2 V2.2 boards have the silkscreen swapped relative to the actual ESP
pins, so try GPIO4 if GPIO5 doesn't transmit.

Any ESP + cheap 433 MHz transmitter (FS1000A / STX882) on any GPIO also
works.

## Usage

1. Copy the `components/aok/` directory next to your ESPHome YAML.
2. See `blinds.yaml` for a complete example.
3. Flash, then for each blind:
   - Hold the PROG button on the motor head until the motor jogs.
   - Within 10 s, press the matching "Pair" button in HA.
   - Motor jogs to confirm.

If a blind's UP/DOWN are reversed after pairing, press its "Reverse"
button once.

## Configuration reference

### `aok:` (the hub)

| Key             | Type     | Default | Description |
|-----------------|----------|---------|-------------|
| `id`            | ID       | —       | Component ID |
| `transmitter_id`| ID       | —       | ID of a `remote_transmitter` |
| `remote_id`     | hex int  | —       | 24-bit virtual remote ID, e.g. `0x8A3F71` |
| `repeats`       | int 1-20 | 8       | How many times to retransmit each frame |

### `cover:` platform `aok`

| Key            | Type        | Default | Description |
|----------------|-------------|---------|-------------|
| `name`         | string      | —       | HA entity name |
| `channel`      | int 1-16    | —       | Channel number (1 = bit 0, etc.) |
| `travel_time`  | duration    | 30 s    | Estimated full open-to-close time |
| `after_delay`  | duration    | 250 ms  | Delay before AFTER (0x24) packet |
| `send_after`   | bool        | true    | Whether to send AFTER after UP/DOWN |

### `button:` platform `aok`

| Key       | Type                          | Description |
|-----------|-------------------------------|-------------|
| `name`    | string                        | HA entity name |
| `cover_id`| ID                            | Which cover to act on |
| `action`  | `pair` \| `change_direction`  | What to send |

## Caveats

- **No position feedback**: A-OK motors don't transmit back. Position is
  estimated from `travel_time`. Set this to your blind's real travel time
  for the estimate to be useful.
- **Intermediate positions**: setting `position: 0.5` will issue UP or
  DOWN and let the motor run, but the component does not currently
  schedule an automatic STOP at the target. Use UP / DOWN / STOP only,
  or rely on the motor's own programmed intermediate stop positions
  (which you set by holding STOP on a real remote).
- **Pair button must be held on the motor head**, not on a remote. If
  your motor doesn't have an accessible PROG button, you'll need to use
  the multi-step procedure documented for your specific motor (often:
  power-cycle then press UP within X seconds).
