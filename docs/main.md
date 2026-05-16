# main.cpp

## Purpose
Primary ESP32 client runtime for Blynk integration, server polling, sensor fetch, and watchdog handling.

## Runtime Loop
- `setup()`:
  - decrypts credentials from LittleFS
  - starts Blynk
  - optional OLED init (`checkSSD()` / `flashSSD()`)
  - schedules periodic `refreshWidgets()` every 20 s
  - starts FreeRTOS support (`initRTOS()`)
  - starts loop watchdog ticker (`LWD_TIMEOUT` = 15 s)
- `loop()`:
  - feeds watchdog
  - runs Blynk
  - runs timer callbacks

## Widget Refresh Path
`refreshWidgets()`:
1. GET sensor list from `ip.php` via `performHttpGet()`
2. parse with `getSensorData()` into `ipMap` sensor:ip and `locMap` location:ip
3. socket poll each server with `ALL`
4. update Blynk terminal only when sensor list changes 
5. update Blynk counters (V7, V20, V19, V34) and last message (V47)
6. on error, writes status to V39 and returns early

## Runtime Maps
- `ipMap` (`map<string,string>`) stores sensor key to IP entries. Keys use the format `<SENSOR>_<n>` to keep duplicate sensor types unique.
- `locMap` (`map<string,string>`) stores IP to location entries for user-facing output.
- Both maps are cleared and rebuilt during each `getSensorData()` refresh cycle so stale nodes are removed.

## Blynk Handlers
- `BLYNK_CONNECTED()`: boot state, reset counters, initial sync, fetch row count
- `BLYNK_WRITE(V18)`: purge IP state from backend via `ipDelete`
- `BLYNK_WRITE(V49)`: terminal command parser (see below)
- `BLYNK_WRITE(BLINK_TST)`: send `BLK` command to all known nodes in `ipMap`
- `BLYNK_WRITE(V10)`: send `RST` command to selected/all known nodes in `ipMap`

Supported terminal commands:
- `list` — show valid commands
- `reboot` — restart ESP32
- `ping` — TCP ping all registered sensors (4 attempts each) + HTTP ping
- `up` — print uptime (days/hours/minutes/seconds)
- `reset` — reset fail/recovered/retry counters
- `refr` — clear `lastSensorsConnected` and force an immediate `refreshWidgets()` cycle
- `all` — iterate all entries in `ipMap` and print one live reading per node via `getSensorData4User()`

Command parsing uses `startsWith(...)`, so a valid command prefix is accepted.

Unrecognised commands return an error message to the terminal.

## Helper Functions
- `performHttpGet(url)` — HTTP GET wrapper, returns response string or empty on failure
- `getSensorData(sensorsConnected)` — parses `"count|sensor:ip,location|..."` into `ipMap` (sensor key -> IP) and `locMap` (IP -> location), then socket-polls each device
- `getSensorData4User(input, ip)` — polls one node (`ALL`), filters token rows by sensor tag, and writes formatted values to terminal pin V49
- `processSensorData(tokens, ip)` — converts sensor codes into device names, derives location from the source IP, sends HTTP updates, and refreshes widgets
- `upDateWidget(sensor, tokens[])` — writes sensor values to Blynk virtual pins; supports BME280, BMP390, SHT35, ADS1115 (DS18B20 and BMP280 are not handled — no matching branch exists)
- `getIP(sensorName)` — case-insensitive substring lookup that returns all matching IPs as a `|`-delimited string with trailing `|` (e.g. `"bme"` matches `"BME280"`)
- `ip2room(ip)` — maps a sensor IP to a room/location label for terminal output
- `blynkWrite(cmd, index)` — maps Blynk button index to sensor labels (`ADC`, `BME`, `SHT`, `BMP`, `DS1`, `BMX`, `ALL`), strips the `_<n>` suffix from `ipMap` keys before matching, sends `BLK`/`RST`, and mirrors response text to `lastMsg` and V47
- `isServerConnected(serverIP, port)` — TCP connect/disconnect reachability check (default port 8888)
- `printUptime()` — formats and writes uptime to Blynk terminal (V49)
- `checkSSD()` — I2C probe for SSD1306 OLED at `0x3C`
- `flashSSD()` — displays "ESP32 Client PIO" and local IP on OLED
- `generateInterrupt()` — manually invokes watchdog ISR for testing
- `ping()` — TCP-pings every entry in `ipMap` 4 times and HTTP-pings `ipList` 4 times; writes pass/dead counts and elapsed time to V49

## getSensorData Flow
1. Read payload header `<rows>` from `"<rows>|..."`.
2. Extract the tuple stream body: `"sensor_or_group:ip,location|..."`.
3. Clear and rebuild runtime maps for a clean refresh cycle:
  - `ipMap`: `<SENSOR>_<n>` -> IP
  - `locMap`: IP -> location
4. For each tuple, split grouped sensor names like `BME_BMP` into separate keys (`BME_<n>`, `BMP_<n+1>`).
5. Poll each parsed IP with socket command `ALL`.
6. On poll failure, queue recovery (`socketRecovery`) and increment `failSocket`.
7. Pass token buffer to `processSensorData()` to update widgets and backend values.

Notes:
- Maps are rebuilt each cycle to remove stale/disconnected devices.
- The function returns the row count parsed from the payload header.

## getSensorData4User Flow
Used by terminal command `all`.

1. Receives a 3-letter sensor prefix and one target IP.
2. Sends socket command `ALL` to that IP.
3. Maps the prefix to expected device tag and scans `tokens[i][0]` for matching rows.
4. Formats result text and writes one line per matched device to V49.

Tag mapping used by the function:
- `bmx` -> `77` (BMP390)
- `bme` -> `76` (BME280)
- `bmp` -> `58` (BMP280, chip ID 0x58)
- `sht` -> `44` (SHT35)
- `adc` -> `48` (ADS1115)
- `ds1` -> `28` (DS18B20)

Output behavior:
- Default output label is `Temp` with unit `F`.
- For `adc`, output label is `Volt` with unit `V`.
- For `adc`, value is scaled by divider ratio (`tokens[i][3]`) before display.
- If socket polling fails, a serial error is printed (`socketClient() failed`).

## Virtual Pin Map
| Pin | Alias | Direction | Description |
|-----|-------|-----------|-------------|
| V2  | GAUGE_HOUSE | write | Jackery voltage (ADS1115: tokens[1] × tokens[3]) |
| V4  | TEMPV4 | write | Temperature (BME280 / BMP390 / SHT35) |
| V6  | TEMPV6 | write | Humidity (BME280 / SHT35) |
| V7  | — | write | passSocket counter |
| V9  | BLINK_TST | read | Send `BLK` to all nodes (disables refresh timer during run) |
| V18 | — | read | Purge IP state from backend |
| V19 | VRECOV | write | recoveredSocket counter |
| V20 | VFAIL | write | failSocket counter |
| V25 | — | write | Last boot time |
| V26 | — | write | Reset reason |
| V34 | VRETRY | write | retry counter |
| V39 | — | write | Error / boot status messages |
| V49 | — | read/write | Terminal (command input + output) |
| V46 | — | write | Terminal refresh start marker (`"Start:"`) when sensor list changes |
| V47 | — | write | Last status / warning message (`lastMsg`), including latest `BLK`/`RST` response from `blynkWrite()` |
| V43 | — | write | ESP32 supply voltage (ADS1115: tokens[2]) |

## Server Endpoints
All hosted on `192.168.1.252`:
| Variable | URL | Purpose |
|----------|-----|---------|
| `ipList` | `/ip.php` | List connected sensors and IPs |
| `ipDelete` | `/deleteIP.php` | Purge IP registrations |
| `getRowCnt` | `/rows.php` | Row count (initialises passSocket) |
| `deleteAll` | `/deleteALL.php` | Delete all records |
| `esp_data` | `/esp-data.php` | Post sensor data |

## Watchdog
- `lwdtFeed()` refreshes loop heartbeat
- `lwdtcb()` restarts on stale loop timing (ISR, placed in IRAM)
