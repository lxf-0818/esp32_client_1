# freeRtos.cpp

Last updated: 2026-07-14

## Purpose
Runs background tasks for queue-driven recovery and SQL HTTP posting, isolated from the main Blynk loop to reduce blocking behavior.

`processSensorData()` is also invoked from recovery context after a successful retried socket read, so recovered payloads are processed the same way as normal reads.

## Core Objects
- QueSocket_Handle: queue for failed socket operations
- QueHTTP_Handle: queue for HTTP post messages
- xMutex_sock: socket synchronization mutex
- xMutex_http: HTTP synchronization mutex
- phpKey: API key string consumed by RTOS HTTP task logic (loaded in login flow via decryptWifiCredentials())
- phpServerIP: base URL prefix used by RTOS HTTP task logic (set during boot/login init)

## Task Topology
initRTOS creates three pinned tasks:
- taskBlink (core 0, priority 1 - low): heartbeat LED toggle
- taskSQL_HTTP (core 0, priority 2): POST sensor lines to backend and validate last-insert bookkeeping
- taskSocketRecov (core 1, priority 3 - high): retry failed socket calls

Queue sizes and stack sizes are compile-time constants.

Current constants:
- SOCKET_QUEUE_SIZE = 2 (for testing)
- HTTP_QUEUE_SIZE = 5
- TASK_STACK_SIZE = 2048 (socket/http tasks use 2×)
- SOCKET_DELAY_MS = 50
- HTTP_DELAY_MS = 100
- BLINK_DELAY_MS = 1000
- MAX_RETRY = 5
- MAX_LINE_LENGTH = 256 (max bytes for HTTP POST payload string in `httpMsg_t`)
- WORDS_PER_BYTE = 4 (stack high-water mark word → byte conversion factor)
- LED_BUILTIN = 2 (GPIO pin for heartbeat LED)

## Queue Data Types

### socket_t
- function pointer for socket operation
- target IP string
- command string
- source location string

Current function pointer signature:
- `int (*fun_ptr)(char *, char *)`

The queued recovery callback currently points to the 2-argument `socketClient` overload.

### message_t
- device name
- encoded post line payload
- key for delete/recovery actions

Note:
The queue payload type in code is `httpMsg_t`. It still contains a `device[10]` field, but `setupHTTP_request()` currently only fills `line` and `key`.

## Tasks

| Task | Core | Priority | Stack | Delay |

| `taskBlink`       | 0 | 1 | TASK_STACK_SIZE | BLINK_DELAY_MS |
| `taskSQL_HTTP`    | 0 | 2 | TASK_STACK_SIZE × 2 | HTTP_DELAY_MS |
| `taskSocketRecov` | 1 | 3 | TASK_STACK_SIZE × 2 | SOCKET_DELAY_MS |

## Main Functions

### initRTOS()
Initializes queues, tasks, mutexes, and GPIO for LED.
Restarts ESP32 if required tasks fail to start.

Failure behavior in initRTOS:
- If any task handle is null after creation, ESP restarts.
- Queue/mutex allocation failures are logged to serial.

### socketRecovery(IP, cmd, sensor)
Pushes failed socket operation to socket queue.
If queue is full, resolves IP -> MAC via `ip2mac()` and calls `deleteMAC.php` when available, then resets the socket queue.

Return behavior:
- `pdTRUE` on successful enqueue
- `errQUEUE_FULL` when full (after cleanup path)
- `10` when queue handle is null

### taskSQL_HTTP(...)
Consumer loop for HTTP queue:
1. receives queued message
2. sends POST to `post-esp-data.php`
3. on HTTP 200, increments task-local `passPost`, reads response body, and compares `passPost` vs returned integer payload
4. if `passPost != payload.toInt()`, logs mismatch and calls `disableTimer()`
5. on non-200 POST, logs failure, calls `disableTimer()`, and immediately continues loop

Additional behavior:
- Uses xMutex_http around HTTP transaction work.
- Tracks `passPost` and `recovered` counters locally in task context (`recovered` is currently not incremented on active path).
- `http.end()` is called on the success path.
- Current non-200 path contains an early `continue`, so cleanup/requeue code below it is unreachable.
- Because of that early `continue`, mutex release for that iteration is also skipped (important runtime caveat).

### taskSocketRecov(...)
Consumes queued socket failures and retries command transmission.
Updates recovery counters and messages.
Recovered last network fail for host:192.168.1.13 
passSocket 85054 failSocket 1  recovered 1 retry 3 

Additional behavior:
- Increments global retry counter before each recovery attempt.
- On successful retry (`socketClient` return code 0), calls `processSensorData(tokens, sensor)`.
- Requeues failed recovery attempts back into QueSocket_Handle.

### setupHTTP_request(sensorName, sensorLocation, tokens)
Builds URL-encoded payload from sensor values and enqueues it into the HTTP queue.

Payload format:
- api_key=<phpKey loaded from /api.txt during login init>
- sensor=<sensorName>
- location=<sensorLocation>
- value1=<tokens[1]>
- value2=<tokens[2]>
- value3=<tokens[3]>
- value4=<passSocket>
- value5=<tokens[4]>

Queue behavior:
- Enqueues only when queue exists and has free space.
- Uses `xQueueSend(..., 0)` (non-blocking); if full, logs an error and drops the message (no reset/retry at enqueue site).

Additional behavior:
- The queued message key is set from `passSocket`.
- Oversized payloads (`length >= MAX_LINE_LENGTH`) log an error and call `disableTimer()`.

### queStat()
Utility to inspect queue state and gate restart behavior when work is still pending.

Notes:
- Waits up to 5 seconds for both queues to drain.
- On success, takes both mutexes before returning true (caller path is typically followed by restart).
- This function releases both mutexes before returning.

## Network Endpoints Used
Current RTOS paths use:
- post-esp-data.php (active POST in `taskSQL_HTTP`)
- deleteMAC.php?key=<macAddress> (active when socket queue overflows in `socketRecovery`)

Also present in helper code (currently not on active task path):
- parse.php?key=<rowKey> (used by `validateLastInsertRow()`)
- delete.php?key=<rowKey> (in unreachable branch below early `continue` in `taskSQL_HTTP`)

## Operational Notes
- Queue backpressure is intentionally small; overflow triggers cleanup strategy.
- Mutex use protects shared HTTP/socket sections while tasks run concurrently.
- This design helps keep the main loop responsive while handling transient network failures.
- If `QueSocket_Handle` is full, `socketRecovery()` clears stale entries with `xQueueReset()` after optional MAC cleanup.
- `taskSocketRecov` annotates recovered token rows with retry count via `setTokens(retryPerIO)` before forwarding to `processSensorData()`.
- `setupHTTP_request()` stores retry count in payload field `value5`.
- Current `taskSQL_HTTP` non-200 branch has an early `continue`; monitor this path carefully since it bypasses cleanup logic and mutex release.

## Unit Test Coverage
Current unit tests for parse/compare behavior live in [test/test_http_parse/test_main.cpp](test/test_http_parse/test_main.cpp).

Covered cases:
- matching `pid` and `key` (`pid == key`)
- mismatching `pid` and `key` (`pid != key`)
- first-comma key parsing behavior
- exact String comparison semantics (e.g., `"105"` vs `"0105"`)

Gap:
The unit tests exercise parsing/comparison behavior in isolation, but they do not cover the current production-side behavior where `validateLastInsertRow()` disables the watchdog timer and still returns success on mismatch.
