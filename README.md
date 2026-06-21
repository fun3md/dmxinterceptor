# DMX Interceptor

ESP32-based transparent DMX pass-through with sACN wireless override, Art-Net forwarding, OSC control, FX macro engine, fixture learning, and a web UI.

## Hardware

- ESP32 DevKit v1 (or ESP32-S3-Zero)
- 2x MAX485 TTL-to-RS485 modules (one RX, one TX)
- XLR connectors for DMX I/O

## Wiring

### MAX485 Module #1 (DMX INPUT - Receive)
| MAX485 Pin | ESP32 Pin | Notes |
|-----------|-----------|-------|
| RO | GPIO 16 | UART1 RX |
| DI | (unused) | — |
| DE + RE | GPIO 4 | Tied together, set LOW = receive |
| VCC | 5V | — |
| GND | GND | — |
| A | XLR Pin 3 | DMX Data+ |
| B | XLR Pin 2 | DMX Data- |

### MAX485 Module #2 (DMX OUTPUT - Transmit)
| MAX485 Pin | ESP32 Pin | Notes |
|-----------|-----------|-------|
| DI | GPIO 33 | UART2 TX |
| RO | (unused) | — |
| DE + RE | GPIO 2 | Tied together, set HIGH = transmit |
| VCC | 5V | — |
| GND | GND | — |
| A | XLR Pin 3 | DMX Data+ |
| B | XLR Pin 2 | DMX Data- |

## Building

Requires [PlatformIO](https://platformio.org/).

```bash
# Build
pio run

# Upload to ESP32
pio run --target upload

# Upload LittleFS (web UI)
pio run --target uploadfs

# Serial monitor
pio device monitor
```

Two environments are configured in `platformio.ini`:

- `esp32dev`        — original ESP-WROOM-32 DevKit
- `esp32-s3-zero`   — ESP32-S3-Zero (PSRAM, USB-CDC)

## Runtime Architecture

The firmware splits work across the two ESP32 cores so that WiFi / network
load can never starve the time-critical DMX driver.

### Core 1 — Real-time DMX (no WiFi, no application code)

| Task           | Priority | Stack | Owns |
|----------------|----------|-------|------|
| `DMX_INIT`     | highest  | 4096  | one-shot `dmx_driver_install()` for both ports |
| `DMX_RX`       | 5        | 4096  | `dmx_receive()` loop, writes `_inputBuffer` |
| `DMX_TX`       | 5        | 4096  | `mergeBuffers()` + `dmx_send()` loop |

Nothing else runs on Core 1. The DMX UART ISRs land here, and the RX/TX
tasks wake directly from the ISR, so WiFi interrupt load on Core 0 cannot
delay a DMX frame.

### Core 0 — Application & network

| Component          | Owner |
|--------------------|-------|
| `APP` FreeRTOS task (priority 1) | WiFi reconnect, sACN, ArtNet, OSC, FX engine, fixture learn, status LED, status print |
| `ESPAsyncWebServer` (system async-TCP task) | HTTP REST API + static `data/www/` |
| WiFi stack (system) | STA / AP, UDP callbacks for sACN / Art-Net / OSC |

### Thread-safety model for the DMX buffers

The DMX buffers are shared across cores and must be accessed carefully:

```
Core 1 (DMX_RX) ──writes──▶ _inputBuffer
Core 1 (DMX_TX) ──reads ──▶ _inputBuffer, _sacnBuffer, _fxOverlay, _fxMask
Core 1 (DMX_TX) ──writes──▶ _outputBuffer
Core 0 (APP)    ──writes──▶ _sacnBuffer, _fxOverlay, _fxMask
Core 0 (web)    ──reads ──▶ _inputBuffer, _outputBuffer
```

Rules enforced in `dmx_engine.{h,cpp}`:

1. `DMXEngine::rxTask()` holds `_bufferMutex` while `dmx_read()` copies into
   `_inputBuffer`, so the web server never sees a half-updated input frame.
2. `DMXEngine::mergeBuffers()` always produces a frame: it takes a
   short-lived snapshot of all source buffers under the mutex (or a
   lock-free fallback if contended), merges into a local copy, and then
   publishes the result to `_outputBuffer`. The previous implementation
   silently skipped the merge when the mutex was contended, which froze
   the DMX output as soon as the web UI started polling
   `/api/dmx/input` and `/api/dmx/output` every 500 ms.
3. Cross-core readers (web API, ArtNet forwarder) use the thread-safe
   snapshot helpers `DMXEngine::copyInputBuffer()` and
   `DMXEngine::copyOutputBuffer()` instead of touching the live buffers.

## Project Phases

- [x] Phase 1: DMX Pass-Through
- [x] Phase 2: WiFi + sACN Override
- [x] Phase 3: Web Interface
- [x] Phase 4: Fixture Learning
- [x] Phase 5: FX Macros
- [x] Phase 6: Smoke Machine
- [ ] Phase 7: Polish & OTA
