# main.cpp

## Purpose
Primary ESP32 client runtime for Blynk integration, backend polling, sensor refresh, terminal utilities, and watchdog handling.

## Runtime loop
- setup():
  - decrypts credentials from LittleFS
  - starts Blynk and connects to Wi-Fi
  - optionally probes the SSD1306 OLED and shows a startup screen
  - schedules refreshWidgets() every 5 seconds
  - starts the loop watchdog ticker with a 15 s timeout
  - initializes FreeRTOS support and performs an initial refresh pass
  - builds the initial netMap/ipMap roster with createMap()
- loop():
  - feeds the watchdog
  - runs Blynk
  - runs timer callbacks

## Data model
- net_t stores one node record with ipAddress, macAddress, and location.
- netMap is the primary sensor-key map keyed as SENSOR_n, for example BME_0.
- ipMap is a de-duplicated IP-based map used by ping and terminal listing.
- tokens[DEVICES][5] holds parsed sensor readings returned by the socket layer.
- passSocket, failSocket, recoveredSocket, and retry track the health and recovery state.

## Widget refresh path
refreshWidgets() currently:
1. calls performHttpGet(ipMacList) to fetch the roster payload
2. calls queHealth() to inspect HTTP queue backlog before continuing
3. calls getSensorData_old() to parse the roster, rebuild netMap, and socket-poll each node
4. updates Blynk counters and status pins (V51, V7, V20, V19, V34, V47)
5. updates the terminal only when the roster payload changes

If the HTTP fetch fails or the parsed sensor count is zero, the function writes a status message to V47 and returns early.

## Blynk handlers
- BLYNK_CONNECTED(): resets counters, loads boot metadata, and initializes the row count from the backend.
- BLYNK_WRITE(V18): clears backend rows through truncate.php.
- BLYNK_WRITE(V49): terminal command parser.
- BLYNK_WRITE(BLINK_TST): sends BLK to the selected sensor group or all nodes.
- BLYNK_WRITE(BOOT): sends RST to the selected sensor group or all nodes.

### Supported terminal commands
- list: shows valid commands
- reboot: drains queues if possible and restarts the ESP32
- ping: checks TCP reachability and HTTP endpoint health
- up: prints uptime
- reset: clears fail/recovered/retry counters
- refr: forces an immediate refresh cycle
- i2c: fetches and prints I2C mappings from each node
- ip: prints the de-duplicated IP/location list
- mac: prints the de-duplicated MAC/location list
- enable / disable: control the periodic refresh timer
- all: polls each known node and prints a live reading summary

Command parsing uses startsWith(), so prefix matches are accepted.

## Helper functions
- performHttpGet(url): HTTP GET wrapper that returns the response body or an empty string on failure.
- getSensorData_old(sensorsConnected): parses the roster payload, rebuilds netMap, and polls each node using socketClient().
- getSensorData_new(): rebuilds netMap/ipMap and can be used for the newer polling flow.
- getSensorData4User(input, ip, room): polls one node and prints filtered readings to the terminal.
- processSensorData(tokens, location): maps sensor codes to names and enqueues HTTP POST payloads.
- blynkWrite(cmd, index): sends BLK or RST to the selected node group.
- dumpIP() / dumpMAC(): print de-duplicated IP/MAC location lists.
- dumpI2C(): requests I2C mappings from the known nodes and prints the parsed tuples.
- ping(): runs TCP and HTTP reachability checks.

## roster payload format
The backend roster is expected in the form:
rows|SENSOR_OR_GROUP:IP,LOCATION-MAC|...|

Example:
2|BME:192.168.1.10,Mud Room-58:BF:25:DA:AE:59|BMX_BME:192.168.1.13,Main Room-48:55:19:ED:B8:B4|

The parser expands grouped names like BME_BMP into separate keys such as BME_0 and BMP_1 before polling the node.

## Virtual pin map
- V2 / GAUGE_HOUSE: Jackery voltage display
- V4 / TEMPV4: temperature output
- V6 / TEMPV6: humidity output
- V7: passSocket counter
- V19 / VRECOV: recoveredSocket counter
- V20 / VFAIL: failSocket counter
- V34 / VRETRY: retry counter
- V43: ESP32 supply voltage display
- V47: last status message
- V49: terminal input/output
- V51: current expanded sensor count

## Server endpoints
The client prepends phpServerIP to the following backend scripts:
- rows.php
- truncate.php
- ip.php
- macip.php or macipTest.php
- deleteMAC.php
- post-esp-data.php

## Watchdog
- lwdtFeed() refreshes the loop heartbeat.
- lwdtcb() restarts the device if the heartbeat is stale or the timeout bookkeeping is inconsistent.

## Notes
- The active refresh path uses getSensorData_old() in the current build.
- The helper getIP() is currently commented out in the source.
- The global tokens buffer is reused across polls, so the code clears it before each socket request.
