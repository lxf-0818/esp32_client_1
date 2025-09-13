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
 *   - `lastMsg`, `failSocket`, `passSocket`, `recoveredSocket`, `retry`: Variables for tracking task statuses.
 *
 * - **Functions**:
 *   - `initRTOS`: Initializes FreeRTOS tasks, queues, and mutexes.
 *   - `socketRecovery`: Adds a failed socket operation to the recovery queue.
 *   - `taskSocketRecov`: Processes socket recovery tasks from the queue.
 *   - `taskSQL_HTTP`: Processes HTTP POST requests from the queue.
 *   - `setupHTTP_request`: Prepares and enqueues an HTTP POST request.
 *   - `taskBlink`: Toggles the built-in LED at regular intervals.
 *   - `queStat`: Checks the status of queues and ensures all tasks are complete.
 *   - `deleteRow`: Deletes a row from the database using a PHP script.
 *   - `socketClient`: Sends a command to a server via a socket connection.
 *
 * - **Structs**:
 *   - `socket_t`: Represents a socket recovery task with a function pointer, IP address, and command.
 *   - `message_t`: Represents an HTTP message with device information, data, and a key.
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
#include <CRC32.h>
#include <Wire.h>

// Constants
// #define DEBUG
#define SOCKET_QUEUE_SIZE 2
#define HTTP_QUEUE_SIZE 5
#define TASK_STACK_SIZE 2048 
#define SOCKET_DELAY_MS 50
#define HTTP_DELAY_MS 2000
#define BLINK_DELAY_MS 1000
#define NO_UPDATE_FAIL 0
#define INPUT_BUFFER_LIMIT 2048
#define MAX_LINE_LENGTH 120
#define LED_BUILTIN 2
#define MAX_RETRY 5
#define WORDS_PER_BYTE 4

// Global Variables
SemaphoreHandle_t xMutex_sock, xMutex_http;
QueueHandle_t QueSocket_Handle, QueHTTP_Handle;
TaskHandle_t socket_task_handle, http_task_handle, blink_task_handle;
extern String lastMsg;
extern int failSocket, passSocket, recoveredSocket, retry;

// Function Prototypes
void initRTOS();
int socketRecovery(char *IP, char *cmd2Send);
void taskSocketRecov(void *pvParameters);
void taskSQL_HTTP(void *pvParameters);
void setupHTTP_request(String sensorName, float tokens[]);
void taskBlink(void *pvParameters);
void taskPing(void *pvParameters);

bool queStat();
int deleteRow(String phpScript);
int socketClient(char *espServer, char *command, bool updateErrorQueue);
// Struct Definitions
typedef struct
{
    int (*fun_ptr)(char *, char *, bool);
    char ipAddr[20];
    char cmd[20];
} socket_t;
socket_t socketQue;

typedef struct
{
    char device[10];
    char line[MAX_LINE_LENGTH];
    int key;
} message_t;
message_t message;

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
 * - Creates three tasks with specific priorities and stack sizes:
 *   - `taskBlink`: Handles LED blinking functionality.
 *   - `taskSQL_HTTP`: Manages HTTP-related operations.
 *   - `taskSocketRecov`: Handles socket recovery operations.
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

    QueHTTP_Handle = xQueueCreate(HTTP_QUEUE_SIZE, sizeof(message_t));
    if (QueHTTP_Handle == NULL)
        Serial.println("Queue could not be created..");

    xTaskCreatePinnedToCore(taskBlink, "Task Blink", TASK_STACK_SIZE, (uint32_t *)&blink_delay, 1, &blink_task_handle, 1);
    xTaskCreatePinnedToCore(taskSQL_HTTP, "Task HTTP", TASK_STACK_SIZE * 2, (uint32_t *)&http_delay, 2, &http_task_handle, 0);
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
 * @brief Sends a socket structure to a FreeRTOS queue for processing.
 *
 * This function attempts to send a `socket_t` structure containing the IP address
 * and command to a FreeRTOS queue. If the queue is full, it resets the queue and
 * optionally deletes a row from a remote database using an HTTP request.
 *
 * @param IP Pointer to a character array containing the IP address.
 * @param cmd2Send Pointer to a character array containing the command to send.
 * @return int `pdTRUE` (1) if the structure was successfully sent to the queue,
 *             `errQUEUE_FULL` (0) if the queue is full, or 10 if the queue handle is NULL.
 *
 * @note Ensure that `QueSocket_Handle` is initialized before calling this function.
 *       If the queue is full, the function will reset the queue and attempt to delete
 *       a row from the database using the provided IP address.
 */
int socketRecovery(char *IP, char *cmd2Send)
{
    socket_t socketQue;
    if (QueSocket_Handle == NULL)
        Serial.println("QueSocket_Handle failed");
    else
    {
        socketQue.fun_ptr = &socketClient;
        strcpy(socketQue.ipAddr, IP);
        strcpy(socketQue.cmd, cmd2Send);
        int ret = xQueueSend(QueSocket_Handle, (void *)&socketQue, 0);
        if (ret == pdTRUE)
        { /* Serial.println("recovering struct send to QueSocket sucessfully"); */
        }
        else if (ret == errQUEUE_FULL)
        {
            Serial.println(".......unable to send data to socket  Queue is Full");
            String phpScript = "http://192.168.1.252/deleteMAC.php?key=" + (String)IP;
            deleteRow(phpScript); // delete 
            //Blynk.logEvent("");
            xQueueReset(QueSocket_Handle);
        }
        return ret;
    }
    return 10;
}

/**
 * @brief Task to log sensor data to a MySQL database using HTTP POST requests.
 *
 * This FreeRTOS task is designed to run on core 0 to handle HTTP operations,
 * allowing core 1 to handle real-time operations. The task retrieves messages from a queue,
 * sends them to a server via HTTP POST, and handles errors by attempting
 * to delete the corresponding row in the database if the POST fails.
 *
 * @param pvParameters Pointer to the delay time (in milliseconds) passed
 *                     as a parameter to the task.
 *
 * @details
 * - The task uses a queue (`QueHTTP_Handle`) to receive messages containing
 *   data to be posted to the server.
 * - A mutex (`xMutex_http`) is used to ensure thread-safe HTTP operations.
 * - If the HTTP POST request succeeds, the task increments the success counter.
 * - If the HTTP POST request fails, the task attempts to delete the corresponding
 *   row in the database by sending a DELETE request to a PHP script. It retries
 *   the deletion up to 5 times with a delay between attempts.
 * - If the deletion is successful, the task re-queues the message for retry.
 * - The task logs various statistics, including the number of successful posts,
 *   failed posts, and recovered messages.
 *
 * @note
 * - The task uses non-blocking delays (`vTaskDelay`) to avoid affecting other tasks.
 * - The HTTP client (`HTTPClient`) and WiFi client (`WiFiClient`) are used for
 *   communication with the php server.
 * - The server URL and PHP script paths are hardcoded in the task.
 *
 *
 */

void taskSQL_HTTP(void *pvParameters)
{
    HTTPClient http;
    // mysql includes
    WiFiClient client_sql;
    String serverName = "http://192.168.1.252/post-esp-data.php";
    int passPost = 0, failPost = 0, recovered = 0;
    uint32_t http_delay = *((uint32_t *)pvParameters);
    TickType_t xDelay = http_delay / portTICK_PERIOD_MS;
    Serial.printf("Task Post SQL running on CoreID:%d xDelay:%u ms Free Bytes: %d\n",
                  xPortGetCoreID(), (unsigned int)xDelay, uxTaskGetStackHighWaterMark(NULL) * WORDS_PER_BYTE);

    for (;;)
    {
        if (QueHTTP_Handle != NULL)
        {
            int ret = xQueueReceive(QueHTTP_Handle, &message, portMAX_DELAY); // wait for message
            if (ret == pdPASS)
            {
                //  "take" blocks calls to esp restart while messages are on queue see queStat()
                xSemaphoreTake(xMutex_http, portMAX_DELAY);
                http.begin(client_sql, serverName.c_str());
                http.addHeader("Content-Type", "application/x-www-form-urlencoded");
                int httpResponseCode = http.POST(message.line);
                if (httpResponseCode > 0)
                {
                    passPost++;
                    String payload = http.getString();
                }
                else
                {
                    String phpScript = "http://192.168.1.252/delete.php?key=" + message.key;
                    Serial.println(phpScript);
                    failPost++;
                    int j = 0, rc = 0;
                    while (1)
                    {
                        vTaskDelay(xDelay); 
                        rc = deleteRow(phpScript);
                        if (rc || j++ == MAX_RETRY)
                            break; //
                    }
                    Serial.printf("rc %d\n", rc);
                    Serial.printf("HTTP Error rc: %d %s %d \n", httpResponseCode, message.line, message.key);
                    Serial.printf("passed %d  failed %d ", passPost, failPost);
                    int ret = xQueueSend(QueHTTP_Handle, (void *)&message, 0); // send message back to queue
                    if (ret == pdTRUE)
                        recovered++;                            //
                    Serial.printf("recoverd %d \n", recovered); // checked mySQL and the entry exists
                }
                http.end();
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
 * 4. Calls the function pointer associated with the socket message to attempt recovery.
 * 5. Updates recovery statistics based on the success or failure of the recovery attempt.
 * 6. If recovery fails, re-queues the socket message for another recovery attempt.
 * 7. Releases the mutex after processing the message.
 *
 * @warning This task assumes that the function pointer in the `socket_t` structure is valid
 *          and callable. Ensure proper validation of the function pointer to avoid undefined behavior.
 */
void taskSocketRecov(void *pvParameters)
{
    socket_t socketQue;
    uint32_t socket_delay = *((uint32_t *)pvParameters);
    const TickType_t xDelay = socket_delay / portTICK_PERIOD_MS;
    Serial.printf("Task Socket Recovery running on CoreID:%d xDelay:%u ms Free Bytes:%d\n",
                  (unsigned int)xPortGetCoreID(), (unsigned int)xDelay, uxTaskGetStackHighWaterMark(NULL) * WORDS_PER_BYTE);
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
                // Serial.printf("socket error %s %s \n", socketQue.ipAddr, socketQue.cmd);
                int x = (*socketQue.fun_ptr)(socketQue.ipAddr, socketQue.cmd, NO_UPDATE_FAIL);
                if (!x)
                {
                    recoveredSocket++;
                    Serial.printf("Recovered last network fail for host:%s s \n", socketQue.ipAddr);
                    Serial.printf("passSocket %d failSocket %d  recovered %d retry %d \n", passSocket, failSocket, recoveredSocket, retry);
                }
                else
                    socketRecovery(socketQue.ipAddr, socketQue.cmd); //  ********SEND Fail to que here for recovery****
                xSemaphoreGive(xMutex_sock);
            }
        }
    }
}

/**
 * @brief Prepares and sends an HTTP request message to a FreeRTOS queue.
 *
 * This function constructs an HTTP request string using the provided sensor name,
 * token values, and a predefined API key and sensor location. It then packages
 * the request into a message structure and attempts to send it to a FreeRTOS queue.
 *
 * @param sensorName The name of the sensor to include in the HTTP request.
 * @param tokens An array of float values used to populate the HTTP request data.
 *               - tokens[1]: First value to include in the request.
 *               - tokens[2]: Second value to include in the request.
 *               - tokens[3]: Key value to associate with the message.
 *
 * @note The function uses an external variable `passSocket` for additional data
 *       in the HTTP request and an external FreeRTOS queue handle `QueHTTP_Handle`.
 *
 * @details The constructed HTTP request string includes the following parameters:
 *          - api_key: A predefined API key,
 *          - sensor: The provided sensor name.
 *          - location: A predefined sensor location ("HOME").
 *          - value1, value2: Values from the `tokens` array.
 *          - value3: The value of the external variable `passSocket`.
 *
 * If the queue is full, the function logs a message to the serial output.
 *
 * @warning Ensure that `QueHTTP_Handle` is initialized and has sufficient space
 *          before calling this function. The function does not block if the queue
 *          is full.
 */
void setupHTTP_request(String sensorName, float tokens[])
{
    message_t message;
    String apiKeyValue = "tPmAT5Ab3j7F9";
    String sensorLocation = "HOME";
    extern int passSocket;

    if (QueHTTP_Handle != NULL && uxQueueSpacesAvailable(QueHTTP_Handle) > 0)
    {
        String httpRequestData = "api_key=" + apiKeyValue;
        httpRequestData += "&sensor=" + sensorName;
        httpRequestData += "&location=" + sensorLocation;
        httpRequestData += "&value1=" + String(tokens[1]);
        httpRequestData += "&value2=" + String(tokens[2]);
        httpRequestData += "&value3=" + String(passSocket) + "";
#ifdef DEBUG
        Serial.printf("http req data %s %d\n", httpRequestData.c_str(), passSocket);
#endif

        strcpy(message.line, httpRequestData.c_str());
        message.key = tokens[3];
        message.line[strlen(message.line)] = 0; // Add the terminating null
        int ret = xQueueSend(QueHTTP_Handle, (void *)&message, 0);
        if (ret == pdTRUE)
        {
            /*  Serial.println(" msg struct send to QueSocket sucessfully"); */
        }
        else if (ret == errQUEUE_FULL)
            Serial.println(".......unable to send data to htpp Queue is Full");
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
                  (unsigned int)xPortGetCoreID(), (unsigned int)xDelay, uxTaskGetStackHighWaterMark(NULL) * WORDS_PER_BYTE);
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
 * @return true If both queues are empty and the mutexes are successfully taken.
 * @return false If the queues are not empty within the 5-second timeout.
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
    return true;
}

/**
 * @brief Struct to hold parameters for the ping task.
 */

/**
 * @brief Task to handle ping operations with two string parameters and return results to the caller.
 *
 * @param pvParameters Pointer to a struct containing two strings and a callback function.
 */
// void taskPing(void *pvParameters)
// {
//     PingParams *params = (PingParams *)pvParameters;

//     Serial.printf("Ping Task started with param1: %s, param2: %s\n", params->param1.c_str(), params->param2.c_str());

//     // Example usage of the parameters
//     for (;;) {
//         Serial.printf("Pinging with %s and %s...\n", params->param1.c_str(), params->param2.c_str());
//         if (params->callback) {
//             params->callback(params->param1, params->param2); // Return results via callback
//         }
//         vTaskDelay(1000 / portTICK_PERIOD_MS);
//     }
// }