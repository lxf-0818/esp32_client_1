# socketClient.cpp

## Purpose
Implements TCP client communication from ESP32 to ESP8266 sensor servers on port 8888.
It requests sensor data, validates integrity, decrypts payload, and parses values
into the shared `tokens[6][5]` matrix.

Widget updates and SQL queue dispatch happen later in `processSensorData()`

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
8. tokenize records into `tokens[6][5]`
9. return 0 on success

> **Note:** `updateErrorQueue` is accepted as a parameter but is currently unused
> (`(void)updateErrorQueue` suppresses the compiler warning). Failure recovery
> (calling `socketRecovery()` and incrementing `failSocket`) is the caller's responsibility.

Failure handling:
- On connect failure, timeout, or CRC mismatch the function returns an error code (1/2/3).
- The caller (`getSensorData` in main.cpp) checks the return code and calls
  `socketRecovery()` / increments `failSocket` as needed.
- Recovery retries are later handled by the FreeRTOS socket recovery task.

### socketClient(espServer, command)
Overload for control commands that returns malloc-allocated response buffer.
Caller must free returned memory.

Current limits/behavior:
- waits up to 10 seconds for first bytes
- allocates a fixed 80-byte response buffer
- restarts ESP32 if malloc fails
- when the command contains `RST`, returns a formatted acknowledgement string
  like `Server 192.168.1.x Was Rebooted` without waiting for a payload
- returns `NULL` on connect failure or timeout

Unknown sensor codes are not handled in this file. They are detected later by
`processSensorData()` which logs `unknow code <id>` and skips those rows.

## Payload Parsing
Records are comma-separated with '|' as row separator.
Example logical shape:
`id,val1,val2,|,id,val1,val2,val3`

Tokenizer writes numeric values into a fixed 6x5 float matrix.
The parser stops when the source string ends; later consumers stop on the first
row whose sensor id is `0`.

## Compile Flags
- `SOCKET_AES` — enabled by default (`#define SOCKET_AES`); enables AES decryption of socket payload.
- `DEBUG_TOKENS` — when defined, calls `printTokens()` to dump the parsed token matrix to Serial after each successful receive.
- `NO_UPDATE_FAIL` — defined as `0`; reserved constant.

## Reliability and Recovery
- timeout and connect failures are queued for retry
- CRC failure is treated as transport/data integrity failure
- `lastMsg`, fail/pass/retry counters are updated for diagnostics
- socket recovery is queue-based: `socketRecovery()` pushes failed polls into
  `QueSocket_Handle`, and `taskSocketRecov()` retries them after a delay

## Integration Points
Called from `getSensorData()` during refresh cycles and from `getSensorData4User()` for terminal-driven live reads.

After a successful `ALL` request:
- `socketClient()` populates the global `tokens` matrix
- `processSensorData()` maps sensor ids to names, resolves location from `maclocMap`,
  updates widgets, and queues HTTP payloads with `setupHTTP_request()`

Works together with the queue and recovery tasks in freeRtos.cpp.
