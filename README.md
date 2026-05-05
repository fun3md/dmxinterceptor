# DMX Interceptor

ESP32-based transparent DMX pass-through with sACN wireless override and FX macro engine.

## Hardware

- ESP32 DevKit v1
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

# Serial monitor
pio device monitor
```

## Project Phases

- [x] Phase 1: DMX Pass-Through
- [ ] Phase 2: WiFi + sACN Override
- [ ] Phase 3: Web Interface
- [ ] Phase 4: Fixture Learning
- [ ] Phase 5: FX Macros
- [ ] Phase 6: Smoke Machine
- [ ] Phase 7: Polish & OTA
