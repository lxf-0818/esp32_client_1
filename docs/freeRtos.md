# FreeRTOS Module Documentation

This document explains the runtime design and public behavior of `src/freeRtos.cpp` in the ESP32 client project.

## Purpose

The module provides:
- FreeRTOS task creation and scheduling
- Queue-based retry pipelines for socket and HTTP failures
- Mutual exclusion for network operations
- A small status gate (`queStat`) used before restart/critical transitions

## Runtime Architecture

### Tasks

- `taskBlink` (core 1, priority 1)
  - Toggles `LED_BUILTIN` at a fixed interval.

- `taskSQL_HTTP` (core 0, priority 2)
  - Dequeues HTTP payloads from `QueHTTP_Handle`.
  - Sends POST requests to the PHP backend.
  - On POST failure, attempts cleanup via `delete.php` and re-queues message.

- `taskSocketRecov` (core 1, priority 3)
  - Dequeues failed socket commands from `QueSocket_Handle`.
  - Re-executes the stored function pointer (`socketClient`).
  - Re-queues again when recovery fails.

### Queues

- `QueSocket_Handle`
  - Item type: `socket_t`
  - Size: `SOCKET_QUEUE_SIZE` (2)
  - Used for deferred socket retry operations.

- `QueHTTP_Handle`
  - Item type: `message_t`
  - Size: `HTTP_QUEUE_SIZE` (5)
  - Used for deferred/retryable HTTP POST operations.

### Mutexes

- `xMutex_sock`
  - Protects socket retry execution path.

- `xMutex_http`
  - Protects HTTP request + retry path.

## Data Structures

### `socket_t`

Fields:
- `fun_ptr`: function pointer of type `int (*)(char *, char *, bool)`
- `ipAddr[20]`: target device/server IP
- `cmd[20]`: command to send

### `message_t`

Fields:
- `device[10]`: reserved/legacy tag field
- `line[MAX_LINE_LENGTH]`: URL-encoded POST body
- `key`: database row key used for cleanup/retry logic

## Public/Externally Used Functions

### `void initRTOS()`

Initializes:
- GPIO mode for `LED_BUILTIN`
- Both queues
- All three tasks pinned to specific cores
- Both mutexes

If any task handle is null after creation, the device is restarted.

### `int socketRecovery(char *IP, char *cmd2Send)`

Enqueues a socket retry item:
- Returns queue status (`pdTRUE`, `errQUEUE_FULL`, or sentinel `10` if queue handle is null).
- If socket queue is full, it resets the queue and attempts row deletion through `deleteMAC.php`.

### `void setupHTTP_request(String sensorName, float tokens[])`

Builds and enqueues a POST payload to `QueHTTP_Handle`:
- Uses static API key and location fields.
- Uses `tokens[1]`, `tokens[2]`, and `tokens[3]` as values/key.
- Applies a scaling branch for names containing `ADS1115`.

Expected token mapping:
- `tokens[1]` -> value1
- `tokens[2]` -> value2
- `tokens[3]` -> row key (also used in optional scaling path)

### `bool queStat()`

Waits until both queues are empty (up to 5 seconds):
- Returns `false` on timeout.
- Returns `true` after queues clear and both mutexes are taken.

## Retry and Recovery Flow

### Socket path

1. Normal socket call fails elsewhere.
2. `socketRecovery` enqueues (`IP`, `cmd`, `socketClient`).
3. `taskSocketRecov` dequeues and retries.
4. If retry still fails, item is re-enqueued.

### HTTP path

1. Sensor payload is prepared via `setupHTTP_request`.
2. `taskSQL_HTTP` dequeues and posts to `post-esp-data.php`.
3. On POST failure, it calls `delete.php?key=...` with retries.
4. Message is enqueued again for another send attempt.

## Configuration Constants (Current Values)

- `SOCKET_QUEUE_SIZE = 2`
- `HTTP_QUEUE_SIZE = 5`
- `TASK_STACK_SIZE = 2048`
- `SOCKET_DELAY_MS = 50`
- `HTTP_DELAY_MS = 100`
- `BLINK_DELAY_MS = 1000`
- `MAX_RETRY = 5`
- `INPUT_BUFFER_LIMIT = 2048`
- `MAX_LINE_LENGTH = 120`
- `LED_BUILTIN = 2`

## Operational Notes

- The module is tightly coupled to hardcoded backend URLs on `192.168.1.252`.
- Queue sizes are intentionally small; sustained failure can cause saturation.
- `queStat()` takes both mutexes and does not release them in this module. Use carefully in system-level shutdown/restart logic.
- The function pointer path in `socket_t` should only carry trusted functions.

## Integration Checklist

- Call `initRTOS()` once during startup after network initialization.
- Ensure globals referenced with `extern` are defined in another translation unit:
  - `lastMsg`
  - `failSocket`
  - `passSocket`
  - `recoveredSocket`
  - `retry`
- Ensure `deleteRow(...)` and `socketClient(...)` implementations are linked and stable under retries.

## Suggested Improvements

- Move API key and backend endpoints to config storage (`data/` or NVS).
- Add queue depth telemetry for proactive back-pressure handling.
- Add bounds-safe copies (`strncpy`) for `ipAddr` and `cmd`.
- Replace magic token indexing with a typed payload struct.
- Add explicit mutex release strategy for `queStat()` lifecycle.
