# main.cpp

## Purpose
Primary ESP32 client runtime for Blynk integration, backend polling, sensor fetch, terminal utilities, and watchdog handling.

## Runtime Loop
- `setup()`:
  - decrypts credentials from LittleFS
  - starts Blynk
  - optional OLED init (`checkSSD()` / `flashSSD()`)
  - schedules periodic `refreshWidgets()` every 20 s
  - starts loop watchdog ticker (`LWD_TIMEOUT` = 15 s)
  - starts FreeRTOS support (`initRTOS()`)
  - runs one immediate refresh (`refreshWidgets()`) to prime runtime counters
  - initializes the sensor roster and IP-location maps used by recovery/deletion helpers (`createMap()`)
- `loop()`:
  - feeds watchdog
  - runs Blynk
  - runs timer callbacks

## Data Model
- `net_t` stores one node record:
  - `ipAddress`
  - `macAddress`
  - `location`
- `netMap` (`map<string, net_t>`) is the primary runtime map.
  - key format: `<SENSOR>_<n>` (for example `BME_0`)
  - value: full node metadata (`net_t`)

## Widget Refresh Path
`refreshWidgets()`:
1. checks Blynk connection and restarts ESP32 if disconnected
2. GET roster from `macip.php` (`ipMacList`) via `performHttpGet()`
3. parses roster with `getSensorData()` and rebuilds `netMap`
4. socket-polls each parsed node with `ALL`
5. updates Blynk counters/status (`V51`, `V7`, `V20`, `V19`, `V34`, `V47`)
6. updates terminal only when roster payload changes (`lastSensorsConnected`)
7. on empty roster HTTP response, writes status to `V39` and returns early
8. on parsed sensor count of zero, writes status to `V39` and returns early

## Blynk Handlers
- `BLYNK_CONNECTED()`: boot state, counter reset, row-count sync, widget label setup
- `BLYNK_WRITE(V18)`: clear backend rows via `deleteAll`
- `BLYNK_WRITE(V49)`: terminal command parser
- `BLYNK_WRITE(BLINK_TST)`: send `BLK` by selected sensor group
- `BLYNK_WRITE(BOOT)`: send `RST` by selected sensor group

Supported terminal commands (`V49`):
- `list` - show valid commands
- `reboot` - restart ESP32
- `ping` - TCP ping all registered nodes (4 attempts each) + HTTP ping backend
- `up` - print uptime (days/hours/minutes/seconds)
- `reset` - reset fail/recovered/retry counters
- `refr` - force an immediate refresh cycle
- `i2c` - fetch and print node I2C mappings
- `ip` - print de-duplicated IP/location list
- `enable` - re-enable the periodic refresh timer
- `disable` - pause the periodic refresh timer
- `all` - print one live reading per registered sensor key

Command parsing uses `startsWith(...)`, so valid command prefixes are accepted.

## Helper Functions
- `performHttpGet(url)` - HTTP GET wrapper, returns payload or empty string on failure
- `getSensorData(sensorsConnected)` - parses roster payload and rebuilds `netMap`; polls each node
- `getSensorData4User(input, ip)` - polls one node and prints filtered reading(s) to terminal
- `processSensorData(tokens, sensorKey)` - maps token device codes to sensor names and forwards data to `setupHTTP_request()`
- `upDateWidget(sensor, tokens[])` - updates selected Blynk virtual pins
  - note: call from `processSensorData()` is currently commented out
- `mac2room(sensorKey)` - resolves location directly from `netMap`
- `blynkWrite(cmd, index)` - maps segmented-button index to sensor groups and sends `BLK`/`RST`
- `dumpIP()` - prints de-duplicated `ipAddress -> location` lines
- `dumpI2C()` - requests `I2C` from each registered key and prints parsed tuples
- `ping()` - TCP/HTTP ping utility with pass/dead summary

## getSensorData Flow
Expected payload format from backend:
`<rows>|<SENSOR_OR_GROUP>:<IP>,<LOCATION>-<MAC>|...|`

Example:
`2|BME:192.168.1.10,Mud Room-58:BF:25:DA:AE:59|BMX_BME:192.168.1.13,Main Room-48:55:19:ED:B8:B4|`

Flow:
1. read row count from header before first `|`
2. clear `netMap`
3. for each tuple, parse sensor/group, IP, location, MAC
4. split grouped names like `BME_BMP` into separate keys (`BME_<n>`, `BMP_<n+1>`)
5. store each key in `netMap`
6. poll node with `socketClient(ip, "ALL")`
7. on failure, queue recovery via `socketRecovery(...)` and increment `failSocket`
8. on success, pass tokens to `processSensorData(...)`

Notes:
- maps are rebuilt each cycle to remove stale/disconnected nodes
- function return value is count of expanded sensor keys inserted

## getSensorData4User Flow
Used by terminal command `all`.

1. receives sensor key text and target IP
2. derives 3-letter prefix and maps it to a device code
3. polls node with socket command `ALL`
4. scans token rows for matching device code
5. writes formatted lines to terminal `V49`

Tag mapping:
- `bmx` -> `77` (BMP390)
- `bme` -> `76` (BME280)
- `bmp` -> `58` (BMP280)
- `sht` -> `44` (SHT35)
- `adc` -> `48` (ADS1115)
- `ds1` -> `28` (DS18B20)

Output behavior:
- default label/units: `Temp`, `F`, `%`
- for `adc`: `Volt`, `V`, `V`
- for `bmp` or `bmx`, second unit is `Pa`
- output format:
  - `<label> <primary_value> <unit1> <secondary_value> <unit2> <location>`

## Virtual Pin Map
| Pin | Alias | Direction | Description |
|-----|-------|-----------|-------------|
| V2  | GAUGE_HOUSE | write | Jackery voltage (ADS1115: `tokens[1]`) |
| V4  | TEMPV4 | write | Temperature (BME280 / BMP390 / SHT35) |
| V6  | TEMPV6 | write | Humidity (BME280 / SHT35) |
| V7  | - | write | passSocket counter |
| V9  | BLINK_TST | read | Segmented control for `BLK` dispatch |
| V10 | - | read | Segmented control for `RST` dispatch |
| V18 | - | read | Clear backend rows (`deleteAll`) |
| V19 | VRECOV | write | recoveredSocket counter |
| V20 | VFAIL | write | failSocket counter |
| V25 | - | write | Last boot time |
| V26 | - | write | Reset reason |
| V34 | VRETRY | write | retry counter |
| V39 | - | write | Error / boot status messages |
| V43 | - | write | ESP32 supply voltage (`tokens[2]`) |
| V46 | - | write | Terminal refresh marker (`"Start:"`) |
| V47 | - | write | Last status / warning message |
| V49 | - | read/write | Terminal input/output |
| V51 | - | write | Current expanded sensor count |

## Server Endpoints
All hosted on `192.168.1.9`:

| Variable | URL | Purpose |
|----------|-----|---------|
| `ipList` | `/ip.php` | List connected sensors and IPs |
| `ipDelete` | `/deleteMAC.php` | Purge MAC registrations |
| `getRowCnt` | `/rows.php` | Row count bootstrap (initialises `passSocket`) |
| `deleteAll` | `/truncate.php` | Delete all backend records |
| `ipMacList` | `/macip.php` (or `/macipTest.php` under `TEST`) | Sensor roster including sensor group, IP, location, MAC |

Notes:
- SQL POST is sent from the RTOS task in `src/freeRtos.cpp` to `/post-esp-data.php`.
- `main.cpp` does not currently use an `esp_data` endpoint constant.

## Watchdog
- `lwdtFeed()` refreshes loop heartbeat
- `lwdtcb()` restarts device if heartbeat timing is stale/inconsistent

## Notes
- `getIP(...)` is currently commented out.
- `getSensorData_new()` exists and refreshes `ipMap` by de-duplicating node IPs, but the active refresh path still uses `getSensorData()` plus `createMap()` for setup and terminal flows.
- `netword` exists as a scratch `net_t` instance but is not used in the active flow.
