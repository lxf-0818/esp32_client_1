# socketClient.cpp

## Purpose
Implements TCP client communication from the ESP32 to sensor nodes on port 8888.
The module requests telemetry, validates integrity, decrypts the payload when AES is enabled, and populates the shared tokens matrix used by the main refresh path.

## Protocol
- transport: TCP
- port: 8888
- typical command: ALL
- control commands: BLK, RST, I2C

Expected sensor response:
`<crc_hex>:<ciphertext>:<iv_hex_csv>`

- CRC is verified over the ciphertext substring between the first and last `:`.
- The IV is the comma-separated hex bytes that follow the last `:` and is parsed into a 16-byte array before decryption.
- After CRC passes, ciphertext is AES-decrypted using the extracted IV to recover the plaintext payload.

The current build path defines SOCKET_AES, so decryption is enabled in normal operation.

## Main APIs

### int socketClient(char *espServer, char *command)
Returns:
- 0 success
- 1 connect failure
- 2 response timeout
- 3 CRC mismatch

Flow:
1. connect to the target ESP server
2. send the command string
3. wait up to 5 seconds for a response
4. read the response text
5. split CRC and payload
6. verify CRC32
7. decrypt the payload when SOCKET_AES is defined
8. tokenize the plaintext into the global tokens matrix
9. return 0 on success

Failure handling:
- On connect failure, timeout, or CRC mismatch, the function returns a non-zero error code.
- On those same failures, lastMsg is updated with a human-readable reason.
- The caller is responsible for queueing recovery and incrementing failSocket.

### char *socketClient(char *espServer, const String &command)
A second overload returns a heap-allocated C-string for control-style commands such as BLK or RST. The caller must free it.

Current behavior:
- waits up to 10 seconds for the first bytes
- allocates a fixed 80-byte buffer
- restarts the ESP32 if malloc fails
- returns NULL on connect failure or timeout

## Payload parsing
Records are comma-separated and separated by `|` row markers. The parser stores numeric values into the shared `tokens[DEVICES][5]` matrix. Unknown sensor IDs are ignored later by processSensorData().

## Compile flags
- SOCKET_AES: enabled by default; enables AES decryption.
- DEBUG_TOKENS: when defined, prints the parsed token matrix after each successful receive.

## Reliability and recovery
- timeout and connect failures are queued for retry by the FreeRTOS socket recovery task
- CRC failure is treated as a data-integrity failure
- lastMsg and the pass/fail/retry counters are updated for diagnostics

## Integration points
Called from getSensorData_old() during refresh cycles and from getSensorData4User() for terminal-driven live reads.

After a successful ALL request:
- socketClient() populates the global tokens matrix
- processSensorData() maps sensor IDs to names, resolves location context, and queues HTTP payloads with setupHTTP_request()
