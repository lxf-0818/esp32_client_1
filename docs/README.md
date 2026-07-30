# ESP32 Client Documentation

Last updated: 2026-07-29

## Overview
This project is an ESP32-based IoT client that:
- decrypts Wi-Fi and Blynk credentials from LittleFS at boot
- connects to Blynk and publishes sensor status to virtual pins
- fetches a sensor roster from a PHP backend over HTTP
- polls sensor nodes over TCP sockets and parses their telemetry
- posts processed sensor values through a FreeRTOS HTTP queue
- uses a software watchdog to reboot the device if the loop stalls

The active runtime entry point is in src/main.cpp, while background recovery and SQL posting live in src/freeRtos.cpp.

## Current implementation highlights
- refreshWidgets() calls performHttpGet(ipMacList), then getSensorData_old() to rebuild the runtime map and poll nodes.
- createMap() rebuilds both netMap and ipMap from the current roster, and is used during setup and terminal-driven flows.
- The watchdog timeout is 15000 ms and is reset by lwdtFeed().
- RTOS tasks handle socket recovery and HTTP POST work so the main loop stays responsive.

## Module docs
- docs/main.md
- docs/cryptography.md
- docs/freeRtos.md
- docs/socketClient.md
- docs/rollBack.md
- docs/misc.md
- docs/blynk_widget.md

## Project structure
- src/main.cpp: setup/loop, Blynk handlers, terminal commands, watchdog logic, widget refresh.
- src/freeRtos.cpp: FreeRTOS task startup, queue consumers, socket recovery, and HTTP POST queueing.
- src/cryptography.cpp: AES-128-CBC helpers and LittleFS credential decryption.
- src/socketClient.cpp: socket protocol handling and token parsing.
- src/misc.cpp: shared helpers and utility functions.
- src/rollBack.cpp: recovery and queue-side helpers.
- src/blynk_widget.h: Blynk virtual pin constants and labels.
- data/: runtime config blobs in LittleFS such as aes.txt, iv.txt, blynkAuth.txt, api.txt, and ssid_pass_aes.txt.

## Runtime flow
1. setup() initializes Serial, decrypts Wi-Fi credentials, starts Blynk, checks the OLED, and starts the watchdog ticker.
2. setup() initializes RTOS support and performs an initial refreshWidgets() pass.
3. refreshWidgets() fetches the roster from the backend and reuses getSensorData_old() to poll each node.
4. Successful socket reads are forwarded to processSensorData(), which fans out sensor values to the HTTP queue.
5. The main loop keeps feeding the watchdog and running Blynk/timer callbacks.

## Data model summary
- net_t: one node record with ipAddress, macAddress, and location.
- netMap: sensor-key map keyed as SENSOR_n, used for roster and terminal output.
- ipMap: de-duplicated IP-based view, used for ping and terminal listing.
- tokens[DEVICES][5]: parsed sensor readings returned by the socket layer.
- passSocket, failSocket, recoveredSocket, retry: counters used by the UI and recovery logic.
- lastSensorsConnected: cached roster payload used to reduce repeated terminal updates.

## Blynk integration
Key handlers in src/main.cpp:
- BLYNK_CONNECTED(): resets counters, loads boot metadata, and initializes the row count from the backend.
- BLYNK_WRITE(V18): clears backend rows via truncate.php.
- BLYNK_WRITE(BLINK_TST): sends BLK to the selected sensor group.
- BLYNK_WRITE(BOOT): sends RST to the selected sensor group.
- BLYNK_WRITE(V49): dispatches terminal commands.

### Terminal commands on V49
- list: shows the command list
- reboot: drains queues if possible and restarts the ESP32
- ping: checks TCP reachability and HTTP endpoint health
- up: prints uptime
- reset: clears fail/recovered/retry counters
- refr: forces an immediate refresh cycle
- i2c: prints I2C mapping from each node
- ip: prints the de-duplicated IP/location list
- mac: prints the de-duplicated MAC/location list
- enable / disable / forceEnable: control refresh timer behavior
- all: polls each known node and prints a live reading summary

## Network endpoints
The client prepends the host/base URL from phpServerIP to the following scripts:
- rows.php
- truncate.php
- ip.php
- macip.php or macipTest.php (when TEST is enabled)
- deleteMAC.php
- post-esp-data.php (used by the RTOS HTTP queue)

The backend base URL is expected to be supplied by data/api.txt at runtime.

## Watchdog behavior
A software watchdog is implemented with Ticker:
- lwdtFeed() updates the heartbeat timestamp.
- lwdtcb() restarts the ESP32 if the loop stalls or the timeout bookkeeping becomes inconsistent.
- The timeout is currently 15000 ms.

## Filesystem configuration
LittleFS files expected in data/:
- aes.txt
- iv.txt
- blynkAuth.txt
- api.txt
- ssid_pass_aes.txt

Upload filesystem data before first boot if values are missing. decryptWifiCredentials() requires the auth and cipher material files to be present.

## Security notes
- Do not commit real auth tokens, keys, or passwords.
- If possible, move the Blynk token and backend host into encrypted files or build-time configuration.
- Consider replacing plain HTTP with authenticated HTTPS when hardware and server constraints allow it.

## Common PlatformIO commands
From the project root:
- pio run
- pio run -t upload
- pio device monitor
- pio run -t uploadfs

## Troubleshooting
- If upload fails while build succeeds, verify the serial port, cable, and board selection.
- If Blynk stays offline, verify the decrypted Wi-Fi credentials and internet reachability.
- If no sensors appear, check the roster endpoint and local network routing.
- If the device reboots often, inspect the serial watchdog messages and reduce blocking work in the main loop and socket paths.
