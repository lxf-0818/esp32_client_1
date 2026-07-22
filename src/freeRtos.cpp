/**
 * @file freeRtos.cpp
 * @brief This file contains the implementation of FreeRTOS-based tasks and queue management for an ESP32 client application.
 *
 * The application includes tasks for handling socket recovery, HTTP requests, and LED blinking. It also manages
 * inter-task communication using FreeRTOS queues and mutexes. The code is designed to handle network errors,
 * log sensor data to a MySQL database, and recover from socket/HTTP failures.
 *
 * @details
 * - **Tasks**:
 *   - `taskBlink`: Toggles the built-in LED at a specified interval.
 *   - `taskSocketRecov`: Handles socket recovery by retrying failed socket operations.
 *   - `taskSQL_HTTP`: Logs sensor data to a MySQL database using HTTP POST requests.
 *
 * - **Queues**:
 *   - `QueSocket_Handle`: Queue for managing socket recovery tasks.
 *   - `QueHTTP_Handle`: Queue for managing HTTP POST requests.
 *
 * - **Mutexes**:
 *   - `xMutex_sock`: Mutex for synchronizing access to socket-related resources.
 *   - `xMutex_http`: Mutex for synchronizing access to HTTP-related resources.
 *
 * - **Constants**:
 *   - `SOCKET_QUEUE_SIZE`: Maximum size of the socket queue.
 *   - `HTTP_QUEUE_SIZE`: Maximum size of the HTTP queue.
 *   - `TASK_STACK_SIZE`: Stack size for each task.
 *   - `SOCKET_DELAY_MS`, `HTTP_DELAY_MS`, `BLINK_DELAY_MS`: Delays for respective tasks.
 *   - `LED_BUILTIN`: GPIO pin for the built-in LED.
 *
 * - **Global Variables**:
 *   - `xMutex_sock`, `xMutex_http`: Mutex handles.
 *   - `QueSocket_Handle`, `QueHTTP_Handle`: Queue handles.
 *   - `socket_task_handle`, `http_task_handle`, `blink_task_handle`: Task handles.
 *   - `failSocket`, `passSocket`, `recoveredSocket`, `retry`: Variables for tracking task statuses.
 *
 * - **Functions**:
 *   - `initRTOS`: Initializes FreeRTOS tasks, queues, and mutexes.
 *   - `socketRecovery`: Adds a failed socket operation to the recovery queue.
 *   - `taskSocketRecov`: Processes socket recovery tasks from the queue.
 *   - `taskSQL_HTTP`: Processes HTTP POST requests from the queue to a PHP endpoint.
 *   - `setupHTTP_request`: Prepares and enqueues an HTTP POST request.
 *   - `taskBlink`: Toggles the built-in LED at regular intervals.
 *   - `queStat`: Checks the status of queues and ensures all tasks are complete.
 *
 * - **Structs**:
 *   - `socket_t`: Represents a socket recovery task with a function pointer, IP address, MAC address and command.
 *   - `httpMsg_t`: Represents an HTTP message with device information, data, and a key.
 *
 * @note The code is designed to run on an ESP32 microcontroller using the Arduino framework.
 * @note The HTTP and socket operations are designed to handle errors and recover gracefully.
 * @note The application uses FreeRTOS features such as tasks, queues, and mutexes for multitasking and synchronization.
 *
 * @author Leon Freimour
 * @date 2025-3-28
 */
#include <Arduino.h>
#include <FS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <Wire.h>
#include <LittleFS.h>

// Constants
// #define DEBUG
#define SOCKET_QUEUE_SIZE 2
#define HTTP_QUEUE_SIZE 10
#define TASK_STACK_SIZE 2048
#define SOCKET_DELAY_MS 50
#define HTTP_DELAY_MS 100
#define BLINK_DELAY_MS 1000
#define INPUT_BUFFER_LIMIT 2048
#define MAX_LINE_LENGTH 256
#define LED_BUILTIN 2
#define MAX_RETRY 5
#define DEVICES 8
#define WORDS_PER_BYTE 4

// Global Variables
SemaphoreHandle_t xMutex_sock, xMutex_http;
QueueHandle_t QueSocket_Handle, QueHTTP_Handle;
TaskHandle_t socket_task_handle, http_task_handle, blink_task_handle;
extern int failSocket, passSocket, recoveredSocket, retry;
extern String phpKey;
extern float tokens[DEVICES][5];
extern String phpServerIP;

bool stop = false;

// Function Prototypes
void initRTOS();
int socketRecovery(char *IP, char *cmd2Send, char *location);
void taskSocketRecov(void *pvParameters);
void taskSQL_HTTP(void *pvParameters);
void setupHTTP_request(const String &sensorName, const String &sensorLocation, float tokens[]);
void taskBlink(void *pvParameters);
void processSensorData(float tokens[][5], const String &sensor);
bool queStat();
int deleteRow(const String &phpScript);
int socketClient(char *espServer, char *command);
void updateBlynk();
String ip2mac(const String &ip);
String performHttpGet(const char *url);
void enableTimer();
void disableTimer();
int validateLastInsertRow(const int row, const String &msg);
String find(String msg, String toFind);
void setTokens(int failCnt);

// Struct Definitions
/**
 * @struct socket_t
 * @brief C-style `typedef struct` alias used for socket recovery task parameters.
 *
 * This declaration creates an anonymous struct and immediately aliases it as
 * `socket_t`, so users can write `socket_t` directly instead of
 * `struct socket_t`. It keeps task queue payload declarations concise.
 * @var fun_ptr Function pointer to the recovery function (typically `socketClient`).
 * @var ipAddr  Null-terminated IP address string (max 20 chars).
 * @var cmd     Null-terminated command string (max 20 chars).
 * @var location Null-terminated location or sensor-key context string (max 20 chars).
 */
typedef struct
{
    int (*fun_ptr)(char *, char *);
    char ipAddr[20];
    char cmd[20];
    char location[20];
} socket_t;
socket_t socketQue;

/**
 * @struct httpMsg_t
 * @brief C-style `typedef struct` alias used for HTTP queue message payloads.
 *
 * Like `socket_t`, this uses `typedef struct` to define and alias the type in
 * one step. The resulting `httpMsg_t` type is passed through FreeRTOS queues
 * and reused across HTTP helper routines.
 * @var device  Device name/identifier (max 10 chars).
 * @var line    Formatted HTTP POST data payload (max MAX_LINE_LENGTH chars).
 * @var key     Numeric key or row ID associated with the message.
 */
typedef struct
{
    char device[10];
    char line[MAX_LINE_LENGTH];
    int key;
} httpMsg_t;
httpMsg_t message;

/**
 * @brief Initializes the FreeRTOS components for the application.
 *
 * This function sets up the necessary FreeRTOS tasks, queues, and mutexes
 * required for the application to function. It performs the following:
 *
 * - Configures the built-in LED pin as an output.
 * - Creates two queues:
 *   - `QueSocket_Handle`: A queue for socket-related data.
 *   - `QueHTTP_Handle`: A queue for HTTP-related messages.
 * - Creates three tasks with specific priorities, stack sizes, and core affinity:
 *   - `taskBlink`: Handles LED blinking functionality (core 0, priority 1).
 *   - `taskSQL_HTTP`: Manages HTTP-related operations (core 0, priority 2).
 *   - `taskSocketRecov`: Handles socket recovery operations (core 1, priority 3).
 *
 * - FreeRTOS Scheduler: Once the above tasks are created, the FreeRTOS scheduler automatically manages their
 *                       execution based on their priorities and delays (vTaskDelay).
 * - Creates two mutexes:
 *   - `xMutex_sock`: A mutex for socket-related synchronization.
 *   - `xMutex_http`: A mutex for HTTP-related synchronization.
 *
 * If any queue or mutex creation fails, an error message is printed to the
 * serial monitor.
 *
 * @note This function assumes that the following macros are defined:
 * - `SOCKET_DELAY_MS`: Delay for socket task.
 * - `HTTP_DELAY_MS`: Delay for HTTP task.
 * - `BLINK_DELAY_MS`: Delay for blink task.
 * - `SOCKET_QUEUE_SIZE`: Size of the socket queue.
 * - `HTTP_QUEUE_SIZE`: Size of the HTTP queue.
 * - `TASK_STACK_SIZE`: Base stack size for tasks.
 * - `LED_BUILTIN`: Pin number for the built-in LED.
 */
void initRTOS()
{
    uint32_t socket_delay = SOCKET_DELAY_MS, http_delay = HTTP_DELAY_MS, blink_delay = BLINK_DELAY_MS;
    pinMode(LED_BUILTIN, OUTPUT);

    QueSocket_Handle = xQueueCreate(SOCKET_QUEUE_SIZE, sizeof(socket_t));
    if (QueSocket_Handle == NULL)
        Serial.println("Queue  socket could not be created..");

    QueHTTP_Handle = xQueueCreate(HTTP_QUEUE_SIZE, sizeof(httpMsg_t));
    if (QueHTTP_Handle == NULL)
        Serial.println("Queue could not be created..");

    xTaskCreatePinnedToCore(taskBlink, "Task Blink", TASK_STACK_SIZE, (uint32_t *)&blink_delay, 1, &blink_task_handle, 0);
    xTaskCreatePinnedToCore(taskSQL_HTTP, "Task HTTP", TASK_STACK_SIZE * 2, (uint32_t *)&http_delay, 2, &http_task_handle, 0);
    // moving the following task to core 0 cause task to trigger internal WD timer ??
    xTaskCreatePinnedToCore(taskSocketRecov, "Task Sockets", TASK_STACK_SIZE * 2, (uint32_t *)&socket_delay, 3, &socket_task_handle, 1);

    if (blink_task_handle == NULL || socket_task_handle == NULL || http_task_handle == NULL)
    {
        Serial.println("tasks not running");
        ESP.restart();
    }
    xMutex_sock = xSemaphoreCreateMutex();
    if (xMutex_sock == NULL)
    {
        Serial.println("Mutex sock can not be created");
    }
    xMutex_http = xSemaphoreCreateMutex();
    if (xMutex_http == NULL)
    {
        Serial.println("Mutex sock can not be created");
    }
}

/**
 * @brief Enqueues a failed socket operation for asynchronous recovery.
 *
 * This function fills a `socket_t` payload with the target host, command, and
 * location/context identifier, then sends it to `QueSocket_Handle`. If the queue is
 * full, it triggers cleanup on the backend and resets the recovery queue.
 *
 * @param IP Pointer to a character array containing the IP address.
 * @param cmd2Send Pointer to a character array containing the command to send.
 * @param location Pointer to a character array containing location or sensor-key context.
 * @return int `pdTRUE` (1) if the structure was successfully sent to the queue,
 *             `errQUEUE_FULL` (0) if the queue is full, or 10 if the queue handle is NULL.
 *
 * @note Ensure that `QueSocket_Handle` is initialized before calling this function.
 *       If the queue is full, the function calls `deleteMAC.php?key=<mac>`, resets
 *       the queue, and clears recovery counters.
 */
int socketRecovery(char *IP, char *cmd2Send, char *location)
{
    if (QueSocket_Handle == NULL)
        Serial.println("QueSocket_Handle failed");
    else
    {
        socketQue.fun_ptr = &socketClient;
        strcpy(socketQue.ipAddr, IP);
        strcpy(socketQue.location, location);
        strcpy(socketQue.cmd, cmd2Send);
        BaseType_t ret = xQueueSend(QueSocket_Handle, (void *)&socketQue, 0);

        if (ret == errQUEUE_FULL)
        {
            Serial.println(".......unable to send data to socket  Queue is Full");
            String macAddr = ip2mac(IP);

            if (!macAddr.isEmpty())
            {
                String phpScript = "deleteMAC.php?key=" + (String)macAddr;
                performHttpGet(phpScript.c_str());
                Serial.printf("PHP Script %s \n", phpScript.c_str());
            }
            else
            {
                Serial.printf("ip not found ipMap %s\n", IP);
            }
            xQueueReset(QueSocket_Handle); // clear stale entries in queue since it is full
            failSocket = retry = recoveredSocket = 0;
        }
        return ret;
    }
    return 10;
}

/**
 * @brief Task to POST sensor data to a PHP endpoint and handle delivery failures.
 *
 * This FreeRTOS task runs on core 0 to handle HTTP POST operations asynchronously.
 * It retrieves messages from a queue, sends them to `post-esp-data.php` via HTTP POST,
 * and handles failures by attempting to delete stale rows via `delete.php`. Successful
 * posts and recovery attempts are logged to track queue health.
 *
 * @param pvParameters Pointer to the delay time (in milliseconds) passed
 *                     as a parameter to the task.
 *
 * @details
 * - Retrieves messages from the `QueHTTP_Handle` queue (blocking indefinitely).
 * - Uses `xMutex_http` to synchronize HTTP operations.
 * - POSTs message to `post-esp-data.php` endpoint .
 * - On success: increments `passSocket` counter.
 * - On failure: attempts to clean up via `deleteRow()` (retry up to MAX_RETRY times),
 *   re-queues the same message, increments `failPost` and `recovered` counters.
 * - Logs diagnostics to Serial (response codes, message content, counters).
 *
 * @note
 * - The task uses non-blocking delays (`vTaskDelay`) to avoid blocking other tasks.
 * - The HTTP endpoint URL is hardcoded: phpServerIP + `post-esp-data.php`.
 * - POST payload is `application/x-www-form-urlencoded` format.
 *
 *
 */

void taskSQL_HTTP(void *pvParameters)
{
    HTTPClient http;
    // mysql includes
    WiFiClient client_sql;
    String serverName = phpServerIP + "post-esp-data.php";
    int passPost = passSocket, recovered = 0;
    uint32_t http_delay = *((uint32_t *)pvParameters);
    TickType_t xDelay = http_delay / portTICK_PERIOD_MS;
    Serial.printf("Task Post SQL running on CoreID:%d xDelay:%u ms Free Bytes: %d\n",
                  xPortGetCoreID(), (unsigned int)xDelay, uxTaskGetStackHighWaterMark(http_task_handle) * WORDS_PER_BYTE);

    for (;;)
    {
        if (QueHTTP_Handle != NULL)
        {
            int ret = xQueueReceive(QueHTTP_Handle, &message, portMAX_DELAY); // wait for message
            if (ret == pdPASS)
            {
                //  "take" blocks calls to esp restart while messages are onh queue see queStat()
                xSemaphoreTake(xMutex_http, portMAX_DELAY);
                http.begin(client_sql, serverName.c_str());
                http.addHeader("Content-Type", "application/x-www-form-urlencoded");
                int httpResponseCode = http.POST(message.line);
#define xxx_
#ifdef xxx

                if (passPost == 5)
                {
                    httpResponseCode = -11;
                }
#endif

                if (httpResponseCode == 200)
                {
                    vTaskDelay(xDelay);
                    passPost++;
                    String msg = message.line;
                    String payload = http.getString();
                    http.end();
                    validateLastInsertRow(passPost, msg.c_str());
                }
                else
                {
                    disableTimer();
                    Serial.printf("last insert failed %d\n", httpResponseCode);
                    String phpScript = "delete.php?key=" + (String)message.key;
                    performHttpGet(phpScript.c_str());

                    Serial.printf("php Script %s\n", phpScript.c_str());
                    // failPost++;

                    int j = 0, rc = 0;
                    // while (1)
                    // {
                    vTaskDelay(xDelay);
                    rc = deleteRow(phpScript);
                    //     if (rc || j++ == MAX_RETRY)
                    //         break; //
                    // }
                    Serial.printf("rc %d\n", rc);
                    Serial.printf("HTTP Error rc: %d %s %d \n", httpResponseCode, message.line, message.key);
                    //  Serial.printf("passed %d  failed %d ", passSocket failPost);
                    int ret = xQueueSend(QueHTTP_Handle, (void *)&message, 0); // send message back to queue
                    if (ret == pdTRUE)
                    {
                        recovered++;
                        passPost++;
                    } //
                    Serial.printf("recoverd %d \n", recovered); // checked mySQL and the entry exists
                    http.end();
                }
                //  http.end();
                vTaskDelay(xDelay);
                xSemaphoreGive(xMutex_http);
            }
            else if (ret == pdFALSE)
                Serial.println("The setSQL_HTTP was unable to receive data from the Queue");
        } // Sanity check
    }
}
/**
 * @brief Task to handle socket recovery by processing messages from a queue.
 *
 * This FreeRTOS task is responsible for recovering failed socket operations.
 * It retrieves socket-related data from a queue, attempts to recover the socket
 * operation, and updates recovery statistics. If recovery fails, the task re-queues
 * the socket operation for another recovery attempt.
 *
 * @param pvParameters Pointer to a uint32_t value specifying the delay (in milliseconds)
 *                     between recovery attempts.
 *
 * The task performs the following steps in an infinite loop:
 * 1. Waits for a socket message from the queue (blocking indefinitely).
 * 2. Takes a mutex to ensure thread-safe access to shared resources.
 * 3. Delays for the specified amount of time before attempting recovery.
 * 4. Calls the queued function pointer (`int (*)(char*, char*)`) to attempt recovery.
 * 5. Updates recovery statistics based on the success or failure of the recovery attempt.
 * 6. If recovery succeeds, invokes `processSensorData(tokens, sensor)`.
 * 7. If recovery fails, re-queues the socket message for another recovery attempt.
 * 8. Releases the mutex after processing the message.
 *
 * @warning This task assumes that the function pointer in the `socket_t` structure is valid
 *          and callable. Ensure proper validation of the function pointer to avoid undefined behavior.
 */
void taskSocketRecov(void *pvParameters)
{
    // moving the following task to core 0 cause task to trigger internal WD timer ??

    socket_t socketQue;
    uint32_t socket_delay = *((uint32_t *)pvParameters);
    const TickType_t xDelay = socket_delay / portTICK_PERIOD_MS;
    int retryPerIO = 0;
    Serial.printf("Task Socket Recovery running on CoreID:%d xDelay:%u ms Free Bytes:%d\n",
                  (unsigned int)xPortGetCoreID(), (unsigned int)xDelay, uxTaskGetStackHighWaterMark(socket_task_handle) * WORDS_PER_BYTE);
    for (;;)
    {
        if (QueSocket_Handle != NULL)
        {
            if (xQueueReceive(QueSocket_Handle, &socketQue, portMAX_DELAY) == pdPASS)
            {
                //"take" blocks calls to esp restart when messages are on queue
                // see queStat()
                xSemaphoreTake(xMutex_sock, portMAX_DELAY);
                vTaskDelay(xDelay);
                retry++;
                retryPerIO++;
                int x = (*socketQue.fun_ptr)(socketQue.ipAddr, socketQue.cmd);
                if (!x)
                {
                    setTokens(retryPerIO); // for all devices
                    processSensorData(tokens, socketQue.location);
                    recoveredSocket++;
                    updateBlynk();
                    Serial.printf("Recovered last network fail for host:%s waiting %d space left %d \n", socketQue.ipAddr,
                                  uxQueueMessagesWaiting(QueSocket_Handle), uxQueueSpacesAvailable(QueSocket_Handle));
                    Serial.printf("passSocket %d failSocket %d  recovered %d retry %d \n", passSocket, failSocket, recoveredSocket, retry);
                }
                else
                    socketRecovery(socketQue.ipAddr, socketQue.cmd, socketQue.location); //  write Fail to que here for recovery****

                xSemaphoreGive(xMutex_sock);
            }
        }
    }
}

/**
 * @brief Builds and enqueues a URL-encoded HTTP POST payload for SQL logging.
 *
 * Creates a form body for `post-esp-data.php` and pushes it to `QueHTTP_Handle`
 * as a `httpMsg_t` item (non-blocking send).
 *
 * Payload format:
 * - `api_key=<phpKey>`
 * - `sensor=<sensorName>`
 * - `location=<sensorLocation>`
 * - `value1=<tokens[1]>`
 * - `value2=<tokens[2]>`
 * - `value3=<tokens[3]>`
 * - `value4=<passSocket>`
 * - `value5=<tokens[4] as retry count>`
 *
 * @param sensorName      Sensor identifier.
 * @param sensorLocation  Logical location label.
 * @param tokens          Float array used by index:
 *                        - tokens[1], tokens[2], tokens[3], tokens[4]
 *
 * @details
 * - Uses a local `httpMsg_t` object and copies request text into `message.line`.
 * - Rejects payloads that do not fit in `message.line` (`MAX_LINE_LENGTH`).
 * - Sets `message.key = passSocket`.
 * - Uses `xQueueSend(..., 0)` (no wait); if queue is full, enqueue fails immediately.
 *
 * @note
 * - The function currently gates enqueue with `uxQueueSpacesAvailable(...) > 0`,
 *   then performs `xQueueSend(...)`. This is non-atomic in multi-task contexts.
 * - If queue handle is null or no slot is available at check time, the function
 *   exits without enqueue.
 *
 * @warning
 * - `tokens` must be valid and contain at least 5 elements.
 * - Indices are intentionally 1-based in this code path.
 */
void setupHTTP_request(const String &sensorName, const String &sensorLocation, float tokens[])
{
    httpMsg_t message;
    int retryCnt = tokens[4];

    if (QueHTTP_Handle != NULL && uxQueueSpacesAvailable(QueHTTP_Handle) > 0)
    {
        String httpRequestData = "api_key=" + phpKey;
        httpRequestData += "&sensor=" + sensorName;
        httpRequestData += "&location=" + sensorLocation;
        httpRequestData += "&value1=" + String(tokens[1]);
        httpRequestData += "&value2=" + String(tokens[2]);
        httpRequestData += "&value3=" + String(tokens[3]);
        httpRequestData += "&value4=" + String(passSocket);
        httpRequestData += "&value5=" + String(retryCnt);
#define DEBUG_
#ifdef DEBUG
        Serial.printf("http req data %s passSocket %d\n", httpRequestData.c_str(), passSocket);
        Serial.flush();
#endif
        if (httpRequestData.length() >= sizeof(message.line))
        {
            Serial.printf("setupHTTP_request: payload too long (%u >= %u)\n",
                          (unsigned)httpRequestData.length(), (unsigned)sizeof(message.line));
            disableTimer();
            return;
        }
        httpRequestData.toCharArray(message.line, sizeof(message.line)); // bounded + null-terminated
        message.key = passSocket;
        // message.line[strlen(message.line)] = 0; // Add the terminating null
        int ret = xQueueSend(QueHTTP_Handle, (void *)&message, 0);
        if (ret == pdTRUE)
        {
            /*  Serial.println(" msg struct send to QueSocket sucessfully"); */
        }
        else if (ret == errQUEUE_FULL)
            Serial.println(".......unable to send data to htpp Queue it's Full");
    }
    else
    {
        if (QueHTTP_Handle != NULL)
            Serial.printf("setupHTTP failed size  %d\n", uxQueueSpacesAvailable(QueHTTP_Handle));
    }
}
/**
 * @brief Task function to blink an LED at a specified interval.
 *
 * This FreeRTOS task toggles the state of the built-in LED (LED_BUILTIN)
 * on and off with a delay specified by the parameter passed to the task.
 * The delay is converted from milliseconds to ticks using the
 * FreeRTOS macro `portTICK_PERIOD_MS`.
 *
 * @param pvParameters Pointer to a uint32_t variable that specifies the
 *                     blink delay in milliseconds. This value is used
 *                     to calculate the delay in ticks for the task.
 *
 * @note The task runs indefinitely in a loop and prints diagnostic
 *       information to the serial monitor, including the core ID it
 *       is running on, the delay in milliseconds, and the remaining
 *       stack space in bytes.
 */
void taskBlink(void *pvParameters)
{
    uint32_t blink_delay = *((uint32_t *)pvParameters);
    const TickType_t xDelay = blink_delay / portTICK_PERIOD_MS;
    Serial.printf("Task Blink running on CoreID:%d xDelay:%u ms Free Bytes: %d\n",
                  (unsigned int)xPortGetCoreID(), (unsigned int)xDelay,
                  uxTaskGetStackHighWaterMark(blink_task_handle) * WORDS_PER_BYTE);
    for (;;)
    {
        digitalWrite(LED_BUILTIN, LOW);
        vTaskDelay(xDelay);
        digitalWrite(LED_BUILTIN, HIGH);
        vTaskDelay(xDelay);
    }
}
/**
 * @brief Checks the status of two FreeRTOS queues and waits until they are empty.
 *
 * This function monitors the message count of two queues (`QueSocket_Handle` and
 * `QueHTTP_Handle`) and waits until both are empty. If the queues are not empty
 * within a 5-second timeout, the function logs a timeout message and returns false.
 * Otherwise, it takes two mutexes (`xMutex_sock` and `xMutex_http`) to ensure
 * exclusive access and logs a completion message before returning true.
 *
 * @return true If both queues are empty and both mutexes are successfully taken.
 * @return false If the queues are not empty within the 5-second timeout.
 *
 * @note This helper briefly takes both mutexes to ensure in-flight queue work
 *       has completed, then releases both before returning.
 */
bool queStat()
{
    unsigned long timeout = millis();

    while (uxQueueMessagesWaiting(QueSocket_Handle) > 0 || uxQueueMessagesWaiting(QueHTTP_Handle) > 0)
    {
        if (millis() - timeout > 5000)
        {
            Serial.println(">>> Queue Timeout!");
            return false;
        }
        Serial.println("Queues are busy...");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    Serial.println("Queues are clear...");

    // if the tasks are running will do a non-block wait unit its done
    xSemaphoreTake(xMutex_sock, portMAX_DELAY);
    xSemaphoreTake(xMutex_http, portMAX_DELAY);
    Serial.println("All tasks complete");
    xSemaphoreGive(xMutex_http);
    xSemaphoreGive(xMutex_sock);

    return true;
}

/**
 * @brief Validates the last SQL insert by comparing parse.php row metadata.
 *
 * Requests parse.php with the provided row ID, expects payload in the form
 * "<pid>|<key>,...", and compares pid against key.
 *
 * Return codes:
 * - 0: payload valid and pid == key.
 * - 1: malformed payload (missing expected delimiter).
 * - 2: mismatch detected (pid != key); stale-row cleanup attempted.
 *
 * @param row 1-based row/checkpoint value used by parse.php lookup.
 * @return int Status code described above.
 */
int validateLastInsertRow(const int row, const String &msg)
{
    String phpScript = "parse.php?key=" + (String)(row);
    String lastInsertResults = performHttpGet(phpScript.c_str());

    // Split `lastInsertResults` as "pid|key,..." and verify insert bookkeeping.
    int index = lastInsertResults.indexOf("|");
    if (index < 0)
    {
        Serial.printf("parse.php failed row %d results last %s \n", row, lastInsertResults.c_str());
        disableTimer();
        return 0;
    }
    String pid = lastInsertResults.substring(0, index);
    int index1 = lastInsertResults.indexOf(",");
    String key = lastInsertResults.substring(index + 1, index1);
    float tokens[5];

    //  test case
    // if (pid == "5")
    //     key = "0";

    if (pid == key)
        return 0;
    else
    {
        disableTimer();
        Serial.printf("passPost %d pid %s key %s\n", row, pid.c_str(), key.c_str());
        return 1;
        HTTPClient http;
        WiFiClient client_sql;
        String serverName = phpServerIP + "post-esp-data.php";

        String tmp, sensor, location, value;
        Serial.printf("parse.php lastInsertResults %s last insert failed .%s. .%s.\n", lastInsertResults.c_str(), pid.c_str(), key.c_str());
        Serial.printf("msg %s\n serveName %s\n", msg.c_str(), serverName.c_str());

        // api_key=tPmAT5Ab3j7F9&sensor=BME280&location=Laundry Room&value1=80.53&value2=59.51&value3=230.65&value4=6&value5=0

        sensor = find(msg, "sensor=");
        Serial.println(sensor);

        location = find(msg, "location=");
        Serial.println(location);

        for (int i = 1; i < 6; i++)
        {
            tmp = ("value" + String(i) + "=");
            value = find(msg, tmp);
            Serial.printf("i %d value %s \n", i, value.c_str());
            tokens[i] = atof(value.c_str());
        }

        String httpRequestData = "api_key=" + phpKey;
        httpRequestData += "&sensor=" + sensor;
        httpRequestData += "&location=" + location;
        httpRequestData += "&value1=" + String(tokens[0]);
        httpRequestData += "&value2=" + String(tokens[1]);
        httpRequestData += "&value3=" + String(tokens[2]);
        httpRequestData += "&value4=" + String(row + 1);
        httpRequestData += "&value5=" + String("1");

        httpRequestData.toCharArray(message.line, sizeof(message.line)); // bounded + null-terminated
        return 0;
        http.begin(client_sql, serverName.c_str());
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        int httpResponseCode = http.POST(message.line);
        http.end();
        if (httpResponseCode == 200)
        {
            Serial.println("retry passed");
            return 0;
        }

        else
        {
            Serial.printf("retry failed rc %d\n", httpResponseCode);
            disableTimer();
            return 2;
        }
    }
}

String find(String msg, String toFind)
{
    int index, index1, index2;
    String tmp, sensor, location;

    index = msg.indexOf(toFind);
    tmp = msg.substring(index);
    index1 = tmp.indexOf("=");
    index2 = tmp.indexOf("&");
    return tmp.substring(index1 + 1, index2);
}
/**
 * @brief Set retry/failure count for active device token rows.
 *
 * Iterates through the global `tokens[DEVICES][5]` table and writes `failCnt`
 * into column `[4]` for each active row. Iteration stops at the first row
 * whose activity marker `tokens[i][0]` is zero.
 *
 * @param failCnt Retry/failure count to store in `tokens[i][4]`.
 *
 * @note A row is considered active when `tokens[i][0] != 0`.
 * @warning This function mutates shared global state; protect with a mutex if
 *          called concurrently from multiple tasks.
 */
void setTokens(int failCnt)
{
    for (int i = 0; i < DEVICES; i++)
    {
        if (!tokens[i][0])
            break;

        tokens[i][4] = failCnt;
    }
}