# freeRtos.cpp

## Purpose
Runs background tasks for queue-driven recovery and SQL HTTP posting, isolated from the main Blynk loop to reduce blocking behavior.

## Core Objects
- QueSocket_Handle: queue for failed socket operations
- QueHTTP_Handle: queue for HTTP post messages
- xMutex_sock: socket synchronization mutex
- xMutex_http: HTTP synchronization mutex

## Task Topology
initRTOS creates three pinned tasks:
- taskBlink (core 1, low priority): heartbeat LED toggle
- taskSQL_HTTP (core 0, medium priority): POST sensor lines to backend
- taskSocketRecov (core 1, high priority): retry failed socket calls

Queue sizes and stack sizes are compile-time constants.

## Queue Data Types

### socket_t
- function pointer for socket operation
- target IP string
- command string

### message_t
- device name
- encoded post line payload
- key for delete/recovery actions

## Main Functions

### initRTOS()
Initializes queues, tasks, mutexes, and GPIO for LED.
Restarts ESP32 if required tasks fail to start.

### socketRecovery(IP, cmd)
Pushes failed socket operation to socket queue.
If queue is full, deletes stale DB row via local PHP endpoint and resets queue.

### taskSQL_HTTP(...)
Consumer loop for HTTP queue:
1. receives queued message
2. sends POST to post-esp-data.php
3. on failure, attempts delete/recovery path with retry limit
4. can requeue recovered message for later send

### taskSocketRecov(...)
Consumes queued socket failures and retries command transmission.
Updates recovery counters and messages.

### setupHTTP_request(sensorName, tokens)
Builds URL-encoded payload from sensor values and enqueues into HTTP queue.

### queStat()
Utility to inspect queue state and gate restart behavior when work is still pending.

## Network Endpoints Used
Hardcoded local endpoints are used for row delete/recovery and post actions, including:
- post-esp-data.php
- delete.php
- deleteIP.php

## Operational Notes
- Queue backpressure is intentionally small; overflow triggers cleanup strategy.
- Mutex use protects shared HTTP/socket sections while tasks run concurrently.
- This design helps keep the main loop responsive while handling transient network failures.
