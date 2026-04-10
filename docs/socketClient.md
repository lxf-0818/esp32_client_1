<<<<<<< HEAD
# Socket Client Module Documentation

This document describes the behavior of src/socketClient.cpp.

## Purpose

The module manages ESP32 TCP socket communication with ESP8266 sensor nodes and bridges received telemetry into:
- local widget updates
- HTTP queueing for backend persistence
- retry/recovery queueing for failed socket interactions

It also validates payload integrity using CRC32 and optionally decrypts payloads with AES.

## Constants

- PORT = 8888
- INPUT_BUFFER_LIMIT = 2048
- MAX_LINE_LENGTH = 120
- NO_UPDATE_FAIL = 0
- SOCKET_AES (enabled by default in this file)

## External State and Dependencies

The module relies on extern globals/functions defined in other translation units:
- lastMsg
- failSocket, passSocket, recoveredSocket, retry
- enc_iv_to[16], aes_iv[16], cleartext[]
- setupHTTP_request(...)
- socketRecovery(...)
- upDateWidget(...)
- decrypt_to_cleartext(...)

## Public Functions

### int socketClient(char *espServer, char *command, bool updateErrorQueue)

High-level behavior:
1. Open TCP connection to espServer:PORT.
2. Send command string.
3. Wait up to 5 seconds for response.
4. Parse response format CRC_HEX:PAYLOAD.
5. Validate payload CRC32 against CRC_HEX.
6. Optionally decrypt PAYLOAD when SOCKET_AES is enabled.
7. Tokenize sensor payload into tokens[5][5].
8. Process sensor rows with processSensorData(...).

Return codes:
- 0: success
- 1: connection failed
- 2: response timeout
- 3: CRC mismatch

Error queue behavior:
- If updateErrorQueue is true, failures enqueue recovery jobs via socketRecovery(...) and increment failSocket.
- If updateErrorQueue is false (recovery mode), failures do not recursively inflate error counters/queue.

Expected payload contract:
- Raw response must contain a colon separator: CRC_HEX:CSV_DATA
- CSV data uses comma separators and pipe marker | between sensor records.

### char *socketClient(char *espServer, char *command)

Legacy overload returning heap memory:
- Connects and sends command.
- Waits up to 35 seconds for response.
- Returns malloc-allocated response buffer (caller must free).
- Returns NULL on connection failure, timeout, or restart path.

Important notes:
- Allocates fixed 80-byte buffer and does not bounds-check incoming response length.
- Restarts MCU on allocation failure.

## Internal Helpers

### void processSensorData(float tokens[5][5], bool updateErrorQueue)

Maps first token in each row to sensor type:
- 77 -> BMP390
- 76 -> BME280
- 58 -> BMP280
- 44 -> SHT35
- 48 -> ADS1115
- 28 -> DS1

For each recognized sensor row:
- increments passSocket
- queues SQL/HTTP payload with setupHTTP_request(...)
- updates widgets with upDateWidget(...)

Unknown sensor codes are ignored.

Note: updateErrorQueue parameter is currently unused in this helper.

### void printTokens(float tokens[5][5])

Debug printer for token matrix contents; intended for diagnostics when debug macro is enabled.

## Token Parsing Model

The decoded payload is parsed with strtok(..., ","):
- numeric values converted with atof(...)
- token | starts a new sensor row

Result layout per row:
- tokens[row][0]: sensor code
- tokens[row][1..]: sensor values/metadata used downstream

## Reliability Flow

Failure handling in primary socketClient path:
- connect fail -> optional queue recovery -> code 1
- timeout -> optional queue recovery -> code 2
- bad CRC -> optional queue recovery -> code 3

Success path:
- parse and process all sensor rows
- returns 0

## Security and Integrity Notes

- CRC32 protects transport integrity against corruption.
- With SOCKET_AES defined, payload confidentiality is provided by decrypting with shared key/IV state.
- IV copy is refreshed before decrypt because decrypt routine mutates IV buffer.

## Known Risks

- Parsing uses strtok on casted String c_str() buffers, which is fragile and can be undefined depending on implementation mutability.
- No explicit bounds guard on read loop for local char str[200] in primary client path.
- Legacy overload uses fixed malloc(80) without input length checks.
- Hardcoded port and protocol assumptions require server/client version lock.

## Suggested Hardening

- Replace strtok on String storage with a mutable owned buffer.
- Add explicit receive length checks before writing into local buffers.
- Replace legacy overload with caller-provided buffer API or dynamic String return strategy.
- Validate response format before substring/index operations.
- Consolidate both socketClient implementations around a shared bounded receive helper.
=======
# Socket Client Module Documentation

This document describes the behavior of src/socketClient.cpp.

## Purpose

The module manages ESP32 TCP socket communication with ESP8266 sensor nodes and bridges received telemetry into:
- local widget updates
- HTTP queueing for backend persistence
- retry/recovery queueing for failed socket interactions

It also validates payload integrity using CRC32 and optionally decrypts payloads with AES.

## Constants

- PORT = 8888
- INPUT_BUFFER_LIMIT = 2048
- MAX_LINE_LENGTH = 120
- NO_UPDATE_FAIL = 0
- SOCKET_AES (enabled by default in this file)

## External State and Dependencies

The module relies on extern globals/functions defined in other translation units:
- lastMsg
- failSocket, passSocket, recoveredSocket, retry
- enc_iv_to[16], aes_iv[16], cleartext[]
- setupHTTP_request(...)
- socketRecovery(...)
- upDateWidget(...)
- decrypt_to_cleartext(...)

## Public Functions

### int socketClient(char *espServer, char *command, bool updateErrorQueue)

High-level behavior:
1. Open TCP connection to espServer:PORT.
2. Send command string.
3. Wait up to 5 seconds for response.
4. Parse response format CRC_HEX:PAYLOAD.
5. Validate payload CRC32 against CRC_HEX.
6. Optionally decrypt PAYLOAD when SOCKET_AES is enabled.
7. Tokenize sensor payload into tokens[5][5].
8. Process sensor rows with processSensorData(...).

Return codes:
- 0: success
- 1: connection failed
- 2: response timeout
- 3: CRC mismatch

Error queue behavior:
- If updateErrorQueue is true, failures enqueue recovery jobs via socketRecovery(...) and increment failSocket.
- If updateErrorQueue is false (recovery mode), failures do not recursively inflate error counters/queue.

Expected payload contract:
- Raw response must contain a colon separator: CRC_HEX:CSV_DATA
- CSV data uses comma separators and pipe marker | between sensor records.

### char *socketClient(char *espServer, char *command)

Legacy overload returning heap memory:
- Connects and sends command.
- Waits up to 35 seconds for response.
- Returns malloc-allocated response buffer (caller must free).
- Returns NULL on connection failure, timeout, or restart path.

Important notes:
- Allocates fixed 80-byte buffer and does not bounds-check incoming response length.
- Restarts MCU on allocation failure.

## Internal Helpers

### void processSensorData(float tokens[5][5], bool updateErrorQueue)

Maps first token in each row to sensor type:
- 77 -> BMP390
- 76 -> BME280
- 58 -> BMP280
- 44 -> SHT35
- 48 -> ADS1115
- 28 -> DS1

For each recognized sensor row:
- increments passSocket
- queues SQL/HTTP payload with setupHTTP_request(...)
- updates widgets with upDateWidget(...)

Unknown sensor codes are ignored.

Note: updateErrorQueue parameter is currently unused in this helper.

### void printTokens(float tokens[5][5])

Debug printer for token matrix contents; intended for diagnostics when debug macro is enabled.

## Token Parsing Model

The decoded payload is parsed with strtok(..., ","):
- numeric values converted with atof(...)
- token | starts a new sensor row

Result layout per row:
- tokens[row][0]: sensor code
- tokens[row][1..]: sensor values/metadata used downstream

## Reliability Flow

Failure handling in primary socketClient path:
- connect fail -> optional queue recovery -> code 1
- timeout -> optional queue recovery -> code 2
- bad CRC -> optional queue recovery -> code 3

Success path:
- parse and process all sensor rows
- returns 0

## Security and Integrity Notes

- CRC32 protects transport integrity against corruption.
- With SOCKET_AES defined, payload confidentiality is provided by decrypting with shared key/IV state.
- IV copy is refreshed before decrypt because decrypt routine mutates IV buffer.

## Known Risks

- Parsing uses strtok on casted String c_str() buffers, which is fragile and can be undefined depending on implementation mutability.
- No explicit bounds guard on read loop for local char str[200] in primary client path.
- Legacy overload uses fixed malloc(80) without input length checks.
- Hardcoded port and protocol assumptions require server/client version lock.

## Suggested Hardening

- Replace strtok on String storage with a mutable owned buffer.
- Add explicit receive length checks before writing into local buffers.
- Replace legacy overload with caller-provided buffer API or dynamic String return strategy.
- Validate response format before substring/index operations.
- Consolidate both socketClient implementations around a shared bounded receive helper.
>>>>>>> e1a7869a6ce7bb9b17e91ab3a080c804531cbe15
