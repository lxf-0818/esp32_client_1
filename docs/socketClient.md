# socketClient.cpp

## Purpose
Implements TCP client communication from ESP32 to ESP8266 sensor servers on port 8888.
It requests sensor data, validates integrity, decrypts payload, parses values,
and dispatches them to widgets and SQL queue handlers.

## Protocol
- transport: TCP
- port: 8888
- typical command: ALL
- control commands: BLK, RST

Expected server response for sensor reads (AES on):
 `<crc_hex>:<ciphertext>:<iv_hex_csv>`

- CRC is verified over the **ciphertext** substring (between first `:` and last `:`).
- IV is the comma-separated hex bytes after the last `:`; parsed into a 16-byte array before decryption.
- After CRC passes, ciphertext is AES-decrypted using the extracted IV to recover the plaintext payload.

If `SOCKET_AES` is disabled (not defined), the server sends `<crc_hex>:<plaintext>` and decryption is skipped.

## Main APIs

### socketClient(espServer, command, updateErrorQueue)
Returns:
- 0 success
- 1 connect failure
- 2 response timeout
- 3 CRC mismatch

Flow:
1. connect to ESP server
2. send command
3. wait up to 5 seconds
4. read response text
5. split CRC and payload
6. verify CRC32
7. decrypt payload when SOCKET_AES is defined
8. tokenize records into `tokens[5][5]`
9. call processSensorData(tokens) only when `updateErrorQueue == true`

Failure handling:
- if `updateErrorQueue == true`: enqueue recovery via `socketRecovery()` and increment `failSocket`
- if `updateErrorQueue == false`: return error code without queue/counter updates

### socketClient(espServer, command)
Overload for control commands that returns malloc-allocated response buffer.
Caller must free returned memory.

Current limits/behavior:
- waits up to 35 seconds for first bytes
- allocates a fixed 80-byte response buffer
- restarts ESP32 if malloc fails

### processSensorData(tokens)
Maps sensor IDs to names and forwards each row to:
- `setupHTTP_request(sensorName, values)`
- `upDateWidget(sensorName, values)`
- increments `passSocket` for each recognized sensor row.

Unknown sensor codes are silently skipped.

Sensor ID mapping:
- 77 -> BMP390
- 76 -> BME280
- 58 -> BMP280
- 44 -> SHT35
- 48 -> ADS1115
- 28 -> DS1

## Payload Parsing
Records are comma-separated with '|' as row separator.
Example logical shape:
`id,val1,val2,|,id,val1,val2,val3`

Tokenizer writes numeric values into a fixed 5x5 float matrix.

## Compile Flags
- `SOCKET_AES` — enabled by default (`#define SOCKET_AES`); enables AES decryption of socket payload.
- `DEBUG_TOKENS` — when defined, calls `printTokens()` to dump the parsed token matrix to Serial after each successful receive.
- `NO_UPDATE_FAIL` — defined as `0`; reserved constant.

## Reliability and Recovery
- timeout and connect failures are queued for retry
- CRC failure is treated as transport/data integrity failure
- `lastMsg`, fail/pass/retry counters are updated for diagnostics

## Integration Points
Called from refresh/update flow in src/main.cpp during widget refresh cycles.
Works together with FreeRTOS queue tasks in src/freeRtos.cpp.

Specific usage in `main.cpp`:
- `refreshWidgets()` / `getSensorData()` call `socketClient(ip, "ALL", 1)` for normal polling
- terminal command handler calls `socketClient(ip, "ALL", 0)` for ad-hoc user reads
- blink-test path uses `socketClient(ip, "BLK")` overload and frees returned buffer
