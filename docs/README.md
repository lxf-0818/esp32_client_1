# ESP32 Client Documentation

Last updated: 2026-06-30

## Overview
This project is an ESP32-based IoT client that:
- Connects to Wi-Fi using encrypted credentials stored in LittleFS.
- Connects to Blynk Cloud and updates multiple virtual pins.
- Polls a local HTTP API for active sensor devices.
- Opens socket connections to ESP sensor nodes to fetch telemetry.
- Displays basic status on an SSD1306 OLED over I2C.
- Uses a loop watchdog timer to reboot on loop stalls.

Main runtime code is in `src/main.cpp`.

Detailed module docs:
- `docs/main.md`
- `docs/cryptography.md`
- `docs/freeRtos.md`
- `docs/socketClient.md`
- `docs/rollBack.md`
- `docs/misc.md`
- `docs/blynk_widget.md`

## Project Structure
- `src/main.cpp`: App entry, Blynk handlers, widget refresh, OLED, watchdog logic.
- `src/socketClient.cpp`: Sensor socket I/O and token parsing.
- `docs/socketClient.md`: Socket protocol, CRC/AES handling, and recovery behavior.
- `src/cryptography.cpp`: AES-128-CBC helpers and Wi-Fi credential decryption.
- `docs/cryptography.md`: AES, LittleFS credential loading, and Wi-Fi decrypt flow.
- `src/freeRtos.cpp`: RTOS task startup and scheduling support.
- `docs/freeRtos.md`: FreeRTOS task, queue, mutex, and recovery flow reference.
- `src/misc.cpp`: Shared utility helpers.
- `src/rollBack.cpp`: Error queue handling and recovery helpers.
- `src/blynk_widget.h`: Virtual pin IDs and widget constants.
- `data/`: Runtime config blobs in LittleFS (AES key, IV, auth token, API URL, encrypted SSID/password).

## Runtime Flow
1. `setup()` starts Serial, decrypts Wi-Fi credentials, and starts Blynk.
2. `setup()` checks OLED and shows startup info if display is present.
3. `setup()` schedules `refreshWidgets()` every 20 seconds.
4. `setup()` starts ticker for the software watchdog.
5. `setup()` initializes RTOS support and runs one immediate widget refresh.
6. `setup()` calls `createMap()` to initialize the IP-to-location map used by recovery helpers.
7. `loop()` continuously runs `lwdtFeed()`, `Blynk.run()`, and `timer.run()`.
8. `refreshWidgets()` fetches current sensor roster from HTTP (`macip.php`), rebuilds runtime state, and updates Blynk stats.
9. Sensor data is read via `socketClient()` and forwarded to backend and selected Blynk widgets.

## Data Model Summary
- `net_t`: One node record containing:
  - `ipAddress`
  - `macAddress`
  - `location`
- `netMap` (`std::map<std::string, net_t>`): Primary runtime map.
  - Key format: `<SENSOR>_<n>` (example: `BME_0`).
  - Value: node metadata (`ipAddress`, `macAddress`, `location`).
- `tokens[DEVICES][5]`: Parsed sensor value matrix from socket payloads.
- `passSocket`, `failSocket`, `recoveredSocket`, `retry`: communication health counters.
- `lastSensorsConnected`: previous roster payload snapshot used to reduce terminal spam.

## Blynk Integration
Important handlers in `src/main.cpp`:
- `BLYNK_CONNECTED()`: Writes boot metadata, resets counters, loads initial stats.
- `BLYNK_WRITE(V49)`: Terminal command parser.
- `BLYNK_WRITE(V18)`: Clears backend sensor rows via `truncate.php`.
- `BLYNK_WRITE(BLINK_TST)`: Sends `BLK` command to selected sensor group (or all).
- `BLYNK_WRITE(V10)`: Sends `RST` command to selected sensor group (or all).

### Terminal Commands on V49
- `list`: show valid commands
- `reboot`: save queue status and reboot
- `ping`: test TCP and HTTP reachability metrics
- `up`: print uptime
- `reset`: reset fail/recovered/retry counters
- `refr`: clear last sensor snapshot and force immediate refresh
- `i2c`: print I2C mapping from each node
- `ip`: print de-duplicated `ipAddress -> location` list
- `all`: iterate all `netMap` entries and print live readings

Command parsing uses `startsWith(...)`, so valid command prefixes are accepted.

## Network Endpoints
The client currently uses fixed local endpoints in `src/main.cpp`:
- `rows.php`
- `truncate.php`
- `ip.php`
- `macip.php` (or `macipTest.php` with `TEST`)
- `deleteMAC.php`
- `post-esp-data.php` (used by RTOS HTTP queue task)

If your server IP changes, update these constants.

Note: `data/api.txt` exists in this project and is loaded by FreeRTOS HTTP queue logic. Host constants in `main.cpp` are still hardcoded.

## Watchdog Behavior
A software loop watchdog is implemented with `Ticker`:
- `lwdtFeed()` updates loop heartbeat timing.
- `lwdtcb()` reboots if loop timing exceeds `LWD_TIMEOUT` after running queue-drain check (`queStat()`).
- Timeout is currently 15000 ms.

## Filesystem Configuration
LittleFS data files expected in `data/`:
- `aes.txt`
- `iv.txt`
- `blynkAuth.txt`
- `api.txt`
- `ssid_pass_aes.txt`

Upload filesystem data before first boot if values are missing.
At startup, `decryptWifiCredentials()` (implemented in `src/cryptography.cpp`) requires `blynkAuth.txt`, `aes.txt`, `iv.txt`, and `ssid_pass_aes.txt` for auth and Wi-Fi decryption.

## Security Notes
- Do not commit real tokens, keys, or passwords.
- Move Blynk auth token and API host into encrypted files or build flags if possible.
- Consider replacing plain HTTP with authenticated HTTPS where hardware and server resources allow.

## Common PlatformIO Commands
From project root:

`pio run`

`pio run -t upload`

`pio device monitor`

`pio run -t uploadfs`

## Troubleshooting
- If upload fails but build passes, verify COM port and USB cable stability.
- If Blynk stays offline, validate decrypted SSID/password and internet reachability.
- If no sensors appear, test `macip.php` endpoint response and local LAN routing.
- If frequent reboots occur, inspect watchdog logs on serial monitor and reduce blocking work in loop callbacks.

## Recommended Next Improvements
- Move hardcoded HTTP endpoint host into `data/api.txt` and load at startup.
- Add retries and timeout metrics around HTTP GET and socket operations.
- Add unit tests for parser logic in `getSensorData()` and command parsing in `BLYNK_WRITE(V49)`.
