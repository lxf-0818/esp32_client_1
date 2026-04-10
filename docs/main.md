# main.cpp

## Purpose
Primary ESP32 client runtime for Blynk integration, server polling, sensor fetch, and watchdog handling.

## Runtime Loop
- `setup()`:
  - decrypts credentials from LittleFS
  - starts Blynk
  - optional OLED init
  - schedules periodic `refreshWidgets()`
  - starts FreeRTOS support
  - starts loop watchdog ticker
- `loop()`:
  - feeds watchdog
  - runs Blynk
  - runs timer callbacks

## Widget Refresh Path
`refreshWidgets()`:
1. GET sensor list from `ip.php`
2. parse with `getSensorData()` into `ipMap`
3. socket poll each server with `ALL`
4. update Blynk counters and terminal output

## Blynk Handlers
- `BLYNK_CONNECTED()`: boot state, reset counters, initial sync
- `BLYNK_WRITE(V18)`: purge IP state from backend
- `BLYNK_WRITE(V42)`: terminal command parser
- `BLYNK_WRITE(BLINK_TST)`: send BLK command to all known nodes

Supported terminal commands:
- list
- reboot
- ping
- up
- adc
- bme
- bmx
- refr

## Watchdog
- `lwdtFeed()` refreshes loop heartbeat
- `lwdtcb()` restarts on stale loop timing
