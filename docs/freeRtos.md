# freeRtos.cpp

Last updated: 2026-07-31

## Purpose
Runs background tasks for queue-driven socket recovery and HTTP POST handling so the main Blynk loop stays responsive. The module also includes a queue-health monitor that can escalate persistent backlog conditions into a reboot or timer shutdown.

## Core objects
- QueSocket_Handle: queue for failed socket operations
- QueHTTP_Handle: queue for HTTP POST messages
- xMutex_sock: mutex protecting socket-related work
- xMutex_http: mutex protecting HTTP-related work
- phpKey: API key string consumed by the HTTP POST payload
- phpServerIP: base URL prefix used by the HTTP task

## Task topology
initRTOS() creates three pinned tasks:
- taskBlink (core 0, priority 1): toggles the built-in LED
- taskSQL_HTTP (core 0, priority 2): sends queued sensor data to post-esp-data.php and validates insert bookkeeping
- taskSocketRecov (core 1, priority 3): retries failed socket operations

## Current constants
- SOCKET_QUEUE_SIZE = 2
- HTTP_QUEUE_SIZE = DEVICES (8)
- TASK_STACK_SIZE = 2048
- SOCKET_DELAY_MS = 50
- HTTP_DELAY_MS = 5
- BLINK_DELAY_MS = 1000
- MAX_RETRY = 5
- MAX_LINE_LENGTH = 256
- WORDS_PER_BYTE = 4
- LED_BUILTIN = 2

## Queue payload types
### socket_t
- function pointer for the socket operation
- target IP string
- command string
- location/context string

### httpMsg_t
- device name field
- encoded POST payload line
- key used for bookkeeping and cleanup paths

## Main functions
### initRTOS()
Creates the queues, mutexes, and tasks. If any required task handle is null, the ESP32 is restarted.

### socketRecovery(IP, cmd, location)
Queues a failed socket operation for later retry. If the socket queue is full, the code resolves the IP to a MAC and calls deleteMAC.php before resetting the queue.

### taskSQL_HTTP(...)
Consumes HTTP messages from QueHTTP_Handle and sends them to post-esp-data.php. On success it validates the last insert via parse.php; on non-200 it disables the timer and exits the current iteration early.

### taskSocketRecov(...)
Consumes queued socket failures, increments retry counters, and retries the socket call. On success it forwards the parsed readings to processSensorData(); on failure it requeues the retry request.

### setupHTTP_request(sensorName, sensorLocation, tokens)
Builds a URL-encoded POST body and sends it to QueHTTP_Handle. The payload contains:
- api_key=<phpKey>
- sensor=<sensorName>
- location=<sensorLocation>
- value1=<tokens[1]>
- value2=<tokens[2]>
- value3=<tokens[3]>
- value4=<passSocket>
- value5=<tokens[4]>

The function uses a non-blocking queue send and drops the message if the queue is full.

### queStat()
Waits for both queues to drain and then takes the mutexes before returning. This is used by the watchdog and terminal reboot path.

### queHealth()
Checks the queued HTTP backlog and, when messages are pending, reports the backlog size, available queue slots, and current pass counter. If queStat() reports that the queue state is still unhealthy, the function restarts the ESP32; otherwise it disables the periodic timer to stop further work in a degraded state.

## Operational notes
- The queue backpressure is intentionally small, so overflow triggers cleanup instead of indefinite backlog growth.
- The code uses mutexes to protect socket and HTTP work while the FreeRTOS tasks run concurrently.
- validateLastInsertRow() currently logs mismatches and disables the timer if the backend validation fails.
- The current active flow uses the queue-based HTTP path from processSensorData() rather than a direct synchronous POST from the main loop.

## Unit test coverage
The current tests in test/test_http_parse/test_main.cpp cover parsing and comparison behavior for insert validation. They do not cover the full runtime queue behavior or the current watchdog disable path.
