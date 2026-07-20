# Three-controller installation close-out

The reference installation is complete: three RC-E5 wall-controller heads
have been replaced with RC-EX3-family controllers, each with a Seeed Studio
XIAO ESP32-C3 bridge. All three controllers are installed, connected to
ESPHome and Home Assistant, and working in normal service.

## Deployed hardware

- Seeed Studio XIAO ESP32-C3 with external antenna
- Pololu D24V7F3 3.3 V buck regulator
- 470 uF / 10 V capacitor across the 3.3 V rail
- Controller TX to GPIO4 (ESP UART RX)
- Controller RX from GPIO5 (ESP UART TX)
- UART at 38400 baud, 8E1

## Deployed behaviour

- Normal status polling every 60 seconds
- Immediate optimistic state publication after Home Assistant commands
- Operational-data polling disabled
- Diagnostic sensor entities omitted
- Three fan speeds plus Auto advertised to Home Assistant
- Automatic heat/cool changeover hidden

The operational-data (`RSR`) transaction was deliberately left disabled after
a controlled test caused the wall controller's "Now communicating with PC"
lockout to persist for tens of seconds. Normal status polling provides the best
balance between wall-panel usability and external-change synchronization.

The component improvements are backed up separately on these fork branches:

- `fix/optimistic-sync-without-extra-polling`
- `feat/configurable-fan-speed-count`

Personal ESPHome YAML, Wi-Fi credentials, API keys, OTA passwords, hostnames,
and network addresses are intentionally not stored in this public repository.
