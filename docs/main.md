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
2. parse with `getSensorData()` into `ipMap`
3. socket poll each server with `ALL`
4. update Blynk terminal (V46) only when sensor list changes
5. update Blynk counters (V7, V20, V19, V34) and last message (V47)
6. on error, writes status to V39 and returns early

## Blynk Handlers
- `BLYNK_CONNECTED()`: boot state, reset counters, initial sync, fetch row count
- `BLYNK_WRITE(V18)`: purge IP state from backend via `ipDelete`
- `BLYNK_WRITE(V46)`: terminal command parser (see below)
- `BLYNK_WRITE(BLINK_TST)`: send `BLK` command to all known nodes in `ipMap`

Supported terminal commands:
- `list` — show valid commands
- `reboot` — restart ESP32
- `ping` — TCP ping all registered sensors (4 attempts each) + HTTP ping
- `up` — print uptime (days/hours/minutes/seconds)
- `adc` — fetch ADS1115 voltage reading
- `bme` — fetch BME280 temperature reading
- `bmx` — fetch BMP390 temperature reading
- `ds1` — fetch DS18B20 temperature reading
- `refr` — force widget refresh and reset fail/recover/retry counters

Unrecognised commands return an error message to the terminal.

## Helper Functions
- `performHttpGet(url)` — HTTP GET wrapper, returns response string or empty on failure
- `getSensorData(sensorsConnected)` — parses `"count|name:ip|..."` string into `ipMap`, socket-polls each device
- `upDateWidget(sensor, tokens[])` — writes sensor values to Blynk virtual pins; supports BME280, BMP390, SHT35, ADS1115
- `getIP(sensorName)` — case-insensitive **substring** lookup of sensor name → IP from `ipMap` (e.g. `"bme"` matches `"BME280"`)
- `isServerConnected(serverIP, port)` — TCP connect/disconnect reachability check (default port 8888)
- `printUptime()` — formats and writes uptime to Blynk terminal (V46) and Serial
- `checkSSD()` — I2C probe for SSD1306 OLED at `0x3C`
- `flashSSD()` — displays "ESP32 Client PIO" and local IP on OLED
- `generateInterrupt()` — manually invokes watchdog ISR for testing

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
| V46 | — | read/write | Terminal (command input + output) |
| V47 | — | write | Last status / warning message (`lastMsg`, updated each refresh cycle) |
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
