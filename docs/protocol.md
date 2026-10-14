# View bundles and USB protocol v1

Editable projects are JSON conforming to the Rust structures in `crates/view-model`. The root contains `version:1`, `start_view`, and `views`. Each view has a nonzero unique ID, name, background color, haptics, LEDs, short/long press destinations (`0` means stay), and widgets. Colors are `0xRRGGBB` integers. Coordinates use a 240×240 canvas and must remain within it; the browser clips the preview to the physical circular display.

## Bundle encoding

An `.skv` file has a 12-byte header followed by definite-length CBOR:

| Bytes | Content |
|---|---|
| 0–3 | ASCII `SKV1` |
| 4–7 | CBOR payload length, unsigned little endian |
| 8–11 | Payload CRC32, unsigned little endian |

CRC32 uses the IEEE reflected polynomial `0xEDB88320`, initial/final XOR `0xFFFFFFFF`.

The CBOR schema uses fixed arrays to keep decoding bounded and allocation-free:

```text
project = [1, start_view, [view, ...]]
view = [id, name, background, haptics, leds, on_press, on_long_press, [widget, ...]]
haptics = [mode, min, max, initial, width_deg, strength, endstop, snap, damping, [detents, ...]]
leds = [mode, brightness, color, accent, offset, reverse, press_flash, mode_flash]
widget = [id, kind, x, y, width, height, color, font_size, text]
```

All five haptic real fields are CBOR float32. Integer widths may use CBOR's shortest encoding. Indefinite lengths, extra fields, trailing data, nonfinite floats, duplicate IDs, dangling navigation, invalid geometry, and over-capacity counts are rejected. Project text is printable ASCII. Bundles are limited to 24,576 bytes; decoded projects use static storage.

Enum values:

| Enum | Values |
|---|---|
| Haptics | free=0, detent=1, bounded=2, magnetic=3, switch=4 |
| LED mode | off=0, solid=1, position=2, progress=3, rainbow=4 |
| Widget | label=0, value=1, arc=2, bar=3, rect=4, circle=5 |

Positions are logical detent numbers. Free/detent/magnetic modes are unbounded within an implementation limit of ±1,000,000. Bounded/switch modes enforce min/max. Switch requires `max=min+1`. View entry sets its initial position and centers that position at the current shaft angle. Arc/bar and LED progress map bounded ranges to 0–1; unbounded indicators map logical travel around one revolution.

## Serial frame

The USB link carries binary frames at a nominal 115200 baud. Integers and float bit patterns are little endian except inside CBOR.

```text
"SK" | version:u8=1 | operation:u8 | sequence:u16 | length:u16 | payload | crc32:u32
```

Payload is at most 512 bytes. Frame CRC covers `version` through the final payload byte, excluding the magic. The parser handles arbitrary read fragmentation, noise, and bad CRCs. Replies use `operation | 0x80`, echo the sequence, and prepend a status byte: 0=success, 1=invalid request, 2=busy, 3=invalid bundle, 4=storage failure, 5=hardware unavailable. Requests are sent sequentially and clients wait for acknowledgment; commands are not automatically retried.

| Operation | Request | Successful response after status byte |
|---|---|---|
| 1 HELLO | empty | Device/version text |
| 2 BEGIN | total size:u32, whole-file CRC:u32 | empty |
| 3 CHUNK | offset:u32, up to 508 data bytes | empty |
| 4 COMMIT | empty | empty; validated project is active in RAM |
| 5 SELECT | view ID:u16 | empty |
| 6 STATE | empty | 28-byte state record |
| 7 CALIBRATE | empty | empty; calibration queued |
| 8 TARE | empty | empty; released-knob tare queued |
| 9 SAVE | empty | empty; uploaded project persisted |
| 10 STOP | empty | empty; motor stop requested |
| 11 ENABLE | empty | empty; valid calibration required |

BEGIN resets the staging transfer. CHUNK must match the exact next offset. COMMIT checks size, whole-file CRC, bundle header/CRC, schema, and references before swapping the project. Interrupted or rejected transfers preserve the active project. SAVE is separate so repeated interactive tests avoid flash writes. It can save the current boot-loaded bundle; factory demo views must first be uploaded to create a persistable bundle. Calibration is persisted independently.

STATE offsets: view ID:u16 at 0; position:i32 at 2; fractional detent position:f32 at 6; faults:u8 at 10; calibration:u8 at 11; loop count:u32 at 12; requested UI-axis voltage:f32 at 16; maximum observed loop period/execution time in microseconds:f32 at 20; press counter:u16 at 24; reserved:u16 at 26. Fractional position grows outside bounded end stops. Calibration is 0=uncalibrated, 1=running, 2=valid. Fault meanings are in [hardware.md](hardware.md).

ACK for motor commands confirms queue acceptance, not physical completion. Use STATE to monitor calibration and fault recovery. This protocol is intentionally versioned independently of the factory protobuf protocol.
