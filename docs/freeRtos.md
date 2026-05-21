# freeRtos.cpp

## Purpose
Runs background tasks for queue-driven recovery and SQL HTTP posting, isolated from the main Blynk loop to reduce blocking behavior.

`processSensorData()` is also invoked from recovery context after a successful retried socket read, so recovered payloads are processed the same way as normal reads.

## Core Objects
- QueSocket_Handle: queue for failed socket operations
- QueHTTP_Handle: queue for HTTP post messages
- xMutex_sock: socket synchronization mutex
- xMutex_http: HTTP synchronization mutex
- phpKey: API key string consumed by RTOS HTTP task logic (loaded in login flow via decryptWifiCredentials())

## Task Topology
initRTOS creates three pinned tasks:
- taskBlink (core 0, priority 1 - low): heartbeat LED toggle
- taskSQL_HTTP (core 0, priority 2): POST sensor lines to backend
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
- MAX_LINE_LENGTH = 120 (max bytes for HTTP POST payload string in `message_t`)
- WORDS_PER_BYTE = 4 (stack high-water mark word → byte conversion factor)
- LED_BUILTIN = 2 (GPIO pin for heartbeat LED)

## Queue Data Types

### socket_t
- function pointer for socket operation
- target IP string
- command string
- source MAC string

Current function pointer signature:
- `int (*fun_ptr)(char *, char *)`

The queued recovery callback currently points to the 2-argument `socketClient` overload.

### message_t
- device name
- encoded post line payload
- key for delete/recovery actions

## Tasks

| Task | Core | Priority | Stack | Delay |
|---|---|---|---|---|
| `taskBlink` | 0 | 1 | TASK_STACK_SIZE | BLINK_DELAY_MS |
| `taskSQL_HTTP` | 0 | 2 | TASK_STACK_SIZE × 2 | HTTP_DELAY_MS |
| `taskSocketRecov` | 1 | 3 | TASK_STACK_SIZE × 2 | SOCKET_DELAY_MS |

## Main Functions

### initRTOS()
Initializes queues, tasks, mutexes, and GPIO for LED.
Restarts ESP32 if required tasks fail to start.

Failure behavior in initRTOS:
- If any task handle is null after creation, ESP restarts.
- Queue/mutex allocation failures are logged to serial.

### socketRecovery(IP, cmd, MAC)
Pushes failed socket operation to socket queue.
If queue is full, calls deleteIP.php for the IP and resets the socket queue.

Return behavior:
- `pdTRUE` on successful enqueue
- `errQUEUE_FULL` when full (after cleanup path)
- `10` when queue handle is null

### taskSQL_HTTP(...)
Consumer loop for HTTP queue:
1. receives queued message
2. sends POST to post-esp-data.php
3. on failure, attempts delete/recovery path with retry limit
4. can requeue recovered message for later send

Additional behavior:
- Uses xMutex_http around HTTP transaction work.
- Tracks passPost/failPost/recovered counters locally in task context.
- Uses `delete.php?key=<id>` as the row cleanup endpoint on POST failure.

### taskSocketRecov(...)
Consumes queued socket failures and retries command transmission.
Updates recovery counters and messages.
Recovered last network fail for host:192.168.1.13 
passSocket 85054 failSocket 1  recovered 1 retry 3 

Additional behavior:
- Increments global retry counter before each recovery attempt.
- On successful retry (`socketClient` return code 0), calls `processSensorData(tokens, ip, mac)`.
- Requeues failed recovery attempts back into QueSocket_Handle.

### setupHTTP_request(sensorName, sensorLocation, tokens)
Builds URL-encoded payload from sensor values and enqueues it into the HTTP queue.

Payload format:
- api_key=<phpKey loaded from /api.txt during login init>
- sensor=<sensorName>
- location=<sensorLocation>
- value1=<tokens[1] or ADS1115-scaled value>
- value2=<tokens[2]>
- value3=<passSocket>

Queue behavior:
- Enqueues only when queue exists and has free space.
- If HTTP queue is full, logs an error and drops the message (no reset/retry at enqueue site).

Additional behavior:
- If `sensorName` contains `ADS1115`, `value1` is multiplied by `tokens[3]` before enqueue.
- The queued message key is set from `tokens[3]`.

### queStat()
Utility to inspect queue state and gate restart behavior when work is still pending.

Notes:
- Waits up to 5 seconds for both queues to drain.
- On success, takes both mutexes before returning true (caller path is typically followed by restart).
- This function does not release those mutexes itself; it is intended for shutdown/restart paths.

## Network Endpoints Used
Hardcoded local endpoints are used for row delete/recovery and post actions, including:
- post-esp-data.php
- delete.php?key=<rowKey>
- deleteIP.php?key=<ipAddress>

## Operational Notes
- Queue backpressure is intentionally small; overflow triggers cleanup strategy.
- Mutex use protects shared HTTP/socket sections while tasks run concurrently.
- This design helps keep the main loop responsive while handling transient network failures.
- If `QueSocket_Handle` is full, `socketRecovery()` clears stale entries with `xQueueReset()` after issuing `deleteIP.php?key=<ip>`.
