# ESPHome A-OK Cover Component

A native ESPHome component for controlling A-OK 433 MHz tubular blind motors
(used by AC114, AC123, AM25 product families, sold under many brands).

This works as a **virtual remote**: you make up a 24-bit remote ID, pair each
motor to it, and ESPHome can then drive all your blinds without ever needing
to clone or capture a physical remote.

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
   - Within 10 s, press the matching "Program" button in HA.
   - Motor jogs to confirm.
   - Use the cover's **Up** button to complete the pairing handshake.

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
| `resume_buffer` | duration | 3 s     | Hub-level default safety buffer for stop-point resume timing (see [Stop points](#stop-points-and-resume-timing)) |
| `resume_factor` | float    | 0.2     | Hub-level default travel-time multiplier for stop-point resume timing |
| `blinds`        | list     | —       | Compact blind definitions (see below) |

### `blinds:` (compact syntax — recommended)

Each entry under `blinds:` automatically creates three Home Assistant
entities for that blind:

- A **Cover** entity with the given name (open, close, stop, position).
- A **Button** entity named `{name} Program` for sending the PROGRAM (0x53) command.
- A **Button** entity named `{name} Reverse` for reversing the motor direction.

To pair a motor: hold its PROG button until it jogs, press **Program** in HA,
then press **Up** on the cover to confirm.

```yaml
aok:
  transmitter_id: tx
  remote_id: 0x8A3F71
  blinds:
    - name: "Bedroom"
      channel: 1
      travel_time: 25s

    - name: "Kitchen"
      channel: 2
      travel_time: 25s
```

| Key           | Type        | Default | Description |
|---------------|-------------|---------|-------------|
| `name`        | string      | —       | HA entity name (also used as prefix for the Program/Reverse buttons) |
| `channel`     | int 1-16    | —       | Channel number |
| `travel_time` | duration    | 30 s    | Estimated full open-to-close time |
| `after_delay` | duration    | 250 ms  | Delay before AFTER (0x24) packet |
| `send_after`  | bool        | true    | Whether to send AFTER after UP/DOWN |
| `inverted`    | bool        | false   | Swap open/close direction in software |
| `stop_points` | list[float] | `[]`    | Percentages (1–99) where the motor has a programmed stop. See below. |
| `resume_buffer` | duration  | inherits hub | Per-blind override of the hub's `resume_buffer` |
| `resume_factor` | float     | inherits hub | Per-blind override of the hub's `resume_factor` |

All standard ESPHome entity options (`icon`, `internal`, `entity_category`,
etc.) are also supported on each blind entry.

### Stop points and resume timing

A-OK motors support programming intermediate **stop points** directly on the
motor (by holding STOP on a real remote while the blind is moving). The
motor will then physically stop itself at that position every time, with no
need for the component to time a STOP command precisely.

If your blind has these programmed, declare them with `stop_points` as a
list of open percentages:

```yaml
blinds:
  - name: "Living Room"
    channel: 1
    travel_time: 50s
    stop_points: [2, 50]
```

With this configured:

- Requesting any target position causes the component to move toward the
  **nearest stop point in that direction first**, rather than running
  straight to the target. The motor self-parks there, and the component
  reads no STOP — it simply updates its position estimate.
- If there's more travel needed to reach the originally requested target,
  the component automatically schedules a **resume**: after a calculated
  delay, it issues another UP/DOWN command to continue toward the next
  stop point (or the target, if no further stop point lies in the way).
- If the requested target itself lands close to a stop point (within ~3%),
  the component snaps to that stop point and lets the motor self-park
  rather than sending a timed STOP.
- For positions that don't correspond to any stop point, the component
  falls back to a timed STOP based on `travel_time`, as before.

**Why the resume needs a delay:** the component's position is only ever an
*estimate* based on `travel_time`. The further the blind has travelled
since the last stop point, the more that estimate can drift from reality.
If the component tried to resume movement as soon as its estimate said
"we should be at the stop point now", it could fire too early — while the
motor is still travelling — and the second command may be ignored or cause
a glitch.

To avoid this, the resume is timed from the **start of the movement**
(not the drifting estimate), using:

```
resume_delay = (estimated travel time to the stop point × resume_factor) + resume_buffer
```

- **`resume_factor`** (default `0.2`) scales the delay with distance: a
  longer stretch of travel gets proportionally more slack, since timing
  error accumulates over distance.
- **`resume_buffer`** (default `3s`) is a small fixed safety margin added
  on top, mainly to cover RF latency and let the motor settle.

Both can be set hub-wide (applies to all blinds) or overridden per blind:

```yaml
aok:
  transmitter_id: tx
  remote_id: 0x8A3F71
  resume_buffer: 3s     # hub default
  resume_factor: 0.2    # hub default
  blinds:
    - name: "Living Room"
      channel: 1
      travel_time: 50s
      stop_points: [2, 50]
      # Longer travel time → more timing uncertainty → a bit more slack
      resume_buffer: 4s
      resume_factor: 0.3

    - name: "Bedroom"
      channel: 2
      travel_time: 30s
      stop_points: [25]
      # No override — uses hub defaults (3s, 0.2×)
```

If you find the blind resuming before the motor has actually parked,
increase `resume_factor` and/or `resume_buffer` for that blind. If resumes
feel sluggish on a blind with accurately-calibrated `travel_time`, you can
lower them.

### Advanced: separate `cover:` and `button:` platforms

If you need finer control (e.g. different entity categories, custom button
names), you can still declare covers and buttons individually:

```yaml
aok:
  id: aok_hub
  transmitter_id: tx
  remote_id: 0x8A3F71

cover:
  - platform: aok
    name: "Bedroom"
    id: blind_bedroom
    aok_id: aok_hub
    channel: 1
    travel_time: 25s

button:
  - platform: aok
    name: "Program Bedroom"
    cover_id: blind_bedroom
    action: program

  - platform: aok
    name: "Reverse Bedroom"
    cover_id: blind_bedroom
    action: change_direction
    entity_category: config
```

#### `cover:` platform `aok`

| Key            | Type        | Default | Description |
|----------------|-------------|---------|-------------|
| `name`         | string      | —       | HA entity name |
| `aok_id`       | ID          | —       | ID of the `aok:` hub |
| `channel`      | int 1-16    | —       | Channel number |
| `travel_time`  | duration    | 30 s    | Estimated full open-to-close time |
| `after_delay`  | duration    | 250 ms  | Delay before AFTER (0x24) packet |
| `send_after`   | bool        | true    | Whether to send AFTER after UP/DOWN |
| `inverted`     | bool        | false   | Swap open/close direction in software |

#### `button:` platform `aok`

| Key        | Type                          | Description |
|------------|-------------------------------|-------------|
| `name`     | string                        | HA entity name |
| `cover_id` | ID                            | Which cover to act on |
| `action`   | `program` \| `change_direction` | What to send |

## Caveats

- **No position feedback**: A-OK motors don't transmit back. Position is
  estimated from `travel_time`. Set this to your blind's real travel time
  for the estimate to be useful.
- **Intermediate positions**: setting `position: 0.5` issues UP or DOWN
  and schedules a timed STOP based on `travel_time`, unless the target is
  close to a configured `stop_point`, in which case the motor self-parks
  instead (see [Stop points and resume timing](#stop-points-and-resume-timing)).
  Timed STOPs are inherently less precise than stop points, since they
  rely entirely on `travel_time` being well calibrated.
- **Pair button must be held on the motor head**, not on a remote. If
  your motor doesn't have an accessible PROG button, you'll need to use
  the multi-step procedure documented for your specific motor (often:
  power-cycle then press UP within X seconds).
