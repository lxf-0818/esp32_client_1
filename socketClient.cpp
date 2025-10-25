/**
 * @file socketClient.cpp
 * @brief Implementation of socket client communication for ESP32.
 *
 * This file contains functions for handling socket communication with an ESP8266 server,
 * processing sensor data, and managing error recovery. It also includes utility functions
 * for handling sensor data and CRC validation.
 *
 * @details
 * - The `socketClient` function handles communication with the server, including sending
 *   commands, receiving data, and validating the received data using CRC.
 * - The `processSensorData` function processes the received sensor data and updates widgets
 *   and sends HTTP requests based on the sensor type.
 * - The `printTokens` function is a debug utility for printing parsed sensor data.
 * - The file also includes an overloaded version of `socketClient` that returns a dynamically
 *   allocated buffer containing the server's response.
 *
 * @note
 * - The `NO_SOCKET_AES` macro disables AES decryption for socket communication.
 * - The `DEBUG` macro enables debug output for token printing.
 * - The file uses a map to associate sensor ids with their corresponding sensor names.
 *
 * @dependencies
 * - Arduino framework
 * - WiFi library for ESP32
 * - CRC32 library for checksum validation
 * - HTTPClient library for HTTP requests
 * - FS library for file system operations
 * - Wire library for I2C communication
 *
 * @author Leon Freimour
 * @date 2025-03-30
 */
#include <Arduino.h>
#include <FS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <CRC32.h>
#include <Wire.h>
#include <map>
#define NO_UPDATE_FAIL 0
#define INPUT_BUFFER_LIMIT 2048
// #define NO_SOCKET_AES
#define MAX_LINE_LENGTH 120
#define PORT 8888

// #define DEBUG

extern String lastMsg;
extern int failSocket, passSocket, recoveredSocket, retry;
extern byte enc_iv_to[16], aes_iv[16];
extern char cleartext[];
void taskSQL_HTTP(void *pvParameters);
void setupHTTP_request(String sensorName, float tokens[]);
int socketRecovery(char *IP, char *cmd2Send);
int socketClient(char *espServer, char *command, bool updateErrorQueue);
void upDateWidget(char *sensor, float tokens[]);
void processSensorData(float tokens[5][5], bool updateErrorQueue);
void printTokens(float tokens[5][5]);
void decrypt_to_cleartext(char *msg, uint16_t msgLen, byte iv[], char *cleartext);

/**
 * @brief Establishes a socket connection to a server, sends a command, and processes the response.
 *
 * @param espServer A pointer to a character array containing the server address.
 * @param command A pointer to a character array containing the command to send to the server.
 * @param updateErrorQueue A boolean flag indicating whether to update the error recovery queue in case of failure.
 *
 * @return int Returns:
 *         - 0 on success.
 *         - 1 if the connection to the server fails.
 *         - 2 if the client times out while waiting for a response.
 *         - 3 if the CRC validation fails.
 *
 * @details
 * The function performs the following steps:
 * 1. Attempts to connect to the server using the provided address and PORT.
 * 2. Sends the specified command to the server if the connection is successful.
 * 3. Waits for a response from the server with a timeout of 5 seconds.
 * 4. Reads the response data and optionally decrypts it if AES encryption is enabled.
 * 5. Validates the response using CRC to ensure data integrity.
 * 6. Parses the response data into tokens and processes the sensor data.
 *
 * If the connection fails, times out, or CRC validation fails, the function updates the error recovery queue
 * (if `updateErrorQueue` is true) and increments the failure counter (`failSocket`).
 *
 * @note The function uses global variables such as `lastMsg`, `failSocket`, and `PORT`.
 *
 * @warning Ensure that the server address and command strings are properly null-terminated.
 *
 * @todo Debug and fix the decryption logic as it is currently not working.
 */
int socketClient(char *espServer, char *command, bool updateErrorQueue)
{
    extern float tokens[5][5];
    char str[80];
    bzero(str, 80);
    WiFiClient client;
    CRC32 crc;

    if (!client.connect(espServer, PORT))
    {
        if (updateErrorQueue)
        {                                       // don't update if in recovery mode ie last i/o failed
            socketRecovery(espServer, command); // current failed write to error recovery queue
            failSocket++;
            Serial.printf(">>> failed to connect: %s!\n", espServer);
            lastMsg = "failed to connect " + String(espServer);
        }
        return 1;
    }

    if (client.connected())
        client.println(command); // send cmd to esp8266 server  ie ALL/BLK/RST

    unsigned long timeout = millis();
    // wait for data to be available
    while (client.available() == 0)
    {
        if (millis() - timeout > 5000)
        {
            Serial.println(">>> Client Timeout!");
            lastMsg = "Client Timeout " + String(espServer);
            client.stop();
            delay(600);
            if (updateErrorQueue)
            {
                socketRecovery(espServer, command); // write to error recovery queque
                failSocket++;
            }
            return 2;
        }
    }
    int index = 0;
    while (client.available())
        str[index++] = client.read(); // read sensor data from sever
    client.stop();

    int calculatedCrc;
    String copyStr = str;

    client.stop();
    index = copyStr.indexOf(":");
    String crcString = copyStr.substring(0, index);
    sscanf(crcString.c_str(), "%x", &calculatedCrc); //convert ASCII string to hex 0xYY
    String parsed = copyStr.substring(index + 1);
    crc.add((uint8_t *)parsed.c_str(), parsed.length());
    if (calculatedCrc != crc.calc())
    {
        lastMsg = "CRC invalid " + String(espServer);
        if (updateErrorQueue)
        {
            socketRecovery(espServer, command); // write to error recovery queque
            failSocket++;
        }
        return 3;
    }
    
#ifndef NO_SOCKET_AES
    // make a copy decrypt_to_cleartext() corrupts byte array aes_iv!
    memcpy(enc_iv_to, aes_iv, sizeof(aes_iv));
    decrypt_to_cleartext((char *)parsed.c_str(), parsed.length(), enc_iv_to, cleartext);
    parsed = String(cleartext);
#endif


    // crc passed !
    memset(tokens, 0, sizeof(tokens));
    char *token = strtok((char *)parsed.c_str(), ",");
    int j = 0, z = 0;
    while (token != NULL)
    {
        if (!strcmp(token, "|"))
        {
            z++;
            j = 0;
        }
        else
            tokens[z][j++] = atof(token);

        token = strtok(NULL, ",");
    }
// #define DEBUG_TOKENS
#ifdef DEBUG_TOKENS
    printTokens(tokens);
#endif
    processSensorData(tokens, updateErrorQueue);

    return 0;
}
/**
 * @brief Processes sensor data and performs actions based on sensor type.
 *
 * This function takes a 2D array of sensor data tokens and processes each sensor's data.
 * It identifies the sensor type using a predefined mapping, then performs actions such as
 * setting up an HTTP request to update mySQL. If an unknown sensor code is encountered,
 * the function continues.
 *
 * @param tokens A 2D array of sensor data, where each row represents a sensor's data.
 *               The first element in each row is the sensor code (as a float).
 * @param updateErrorQueue A boolean flag indicating whether to update the error queue.
 *                         (Currently unused due to a resolved bug.)
 *
 * @note The function uses a predefined mapping of sensor codes to sensor names for identification.
 *       If a sensor code is not found in the mapping, the function continues to next.
 * @note A previous bug related to "Stack canary" exceptions was resolved by increasing the stack size.
 * @note A previous bug related to "Stack canary" exceptions was resolved by increasing the stack size.
 */
void processSensorData(float tokens[5][5], bool updateErrorQueue)
{
    const std::map<int, const char *> sensorMap =
        {
            {77, "BMP390"},
            {76, "BME280"},
            {58, "BMP280"},
            {44, "SHT35"},
            {48, "ADS1115"},
            {28, "DS1"}};

    char sensor[10];

    for (int i = 0; i < 5; i++)
    {
        int sensorCode = static_cast<int>(tokens[i][0]);
        auto it = sensorMap.find(sensorCode);
        if (it != sensorMap.end())
        {
            strcpy(sensor, it->second);
            passSocket++;
            setupHTTP_request(sensor, tokens[i]);
            upDateWidget(sensor, tokens[i]);
        }
        else
            continue; // Unknown sensor code
    }
}
/**
 * @brief Prints the contents of a 2D array of tokens to the Serial monitor.
 *
 * This function iterates through a 5x5 array of floating-point numbers and
 * prints each row to the Serial monitor. The first element of each row is
 * treated as a sensor ID and is printed in hexadecimal format, while the
 * remaining elements are printed as floating-point numbers. The function
 * stops processing rows when the first element of a row is zero.
 *
 * @param tokens A 5x5 array of floating-point numbers representing the tokens.
 *               The first element of each row is treated as a sensor ID.
 */
void printTokens(float tokens[5][5])
{
    for (int i = 0; i < 5; i++)
    {
        if (!tokens[i][0])
            break;

        for (int j = 0; j < 5; j++)
        {
            if (j == 0)
                Serial.printf("sensor id: 0x%d ", static_cast<int>(tokens[i][j]));
            else
                Serial.printf("%f ", tokens[i][j]);
        }
        Serial.println();
    }
}
/**  this overload returns malloc its your duty to free!!!
 * @brief Establishes a socket connection to a server, sends a command, and retrieves the response.
 *
 * @param espServer A pointer to a character array containing the server's address.
 * @param command A pointer to a character array containing the command to send to the server.
 * @return A pointer to a dynamically allocated character array containing the server's response.
 *         Returns NULL if the connection fails, a timeout occurs, or memory allocation fails.
 *
 * @note The caller is responsible for freeing the memory allocated for the response using free().
 *
 * @details
 * - The function attempts to connect to the specified server using the WiFiClient class.
 * - If the connection is successful, it sends the provided command to the server.
 * - The function waits for a response from the server, with a timeout of 35 seconds.
 * - If no response is received within the timeout period, the connection is closed, and NULL is returned.
 * - The response from the server is read into a dynamically allocated buffer.
 * - If memory allocation fails, the ESP device is restarted.
 * - The connection is closed after reading the response.
 *
 * @warning Ensure that free() is called on the returned pointer to avoid memory leaks.
 * @warning The function restarts the ESP device if memory allocation fails.
 *
 * @example
 * char *response = socketClient("192.168.1.100", "BLK");
 * if (response != NULL) {
 *     Serial.println(response);
 *     free(response);
 * } else {
 *     Serial.println("Failed to get a response from the server.");
 * }
 */
char *socketClient(char *espServer, char *command)
{
    // char *socketClient(char *espServer, char *command)
    // {
    //   // Placeholder implementation for socketClient
    //   char *response = (char *)malloc(100);
    //   if (response == nullptr)
    //   {
    //     Serial.println("Memory allocation failed");
    //     return nullptr;
    //   }
    //   snprintf(response, 100, "Response from %s with command %s", espServer, command);
    //   return response;
    // }
    int j = 0;
    WiFiClient client;
    if (!client.connect(espServer, PORT))
    {
        Serial.print("connection failed from socketClient ");
        Serial.println(espServer);
        delay(5000);
        return NULL;
    }
    if (client.connected())
        client.println(command); // send cmd to server (esp8266) ie "BLK"/"RST"

    unsigned long timeout = millis();
    // wait for data to be available
    while (client.available() == 0)
    {
        if (millis() - timeout > 35000)
        {
            Serial.println(">>> Client Timeout !");
            client.stop();
            delay(600);
            return NULL;
        }
    }
    char *mem = (char *)malloc(80);
    if (mem == NULL)
    {
        //  did you call free()?
        // Blynk.logEvent("mem_alloc_failed");
        // queStat();
        ESP.restart();
    }
    // read sensor data from sever
    while (client.available())
    { // read data from server (esp8266)
        char ch = static_cast<char>(client.read());
        mem[j++] = ch;
    }

    // Close the connection
    client.stop();
    // Serial.println("closing connection");

    mem[j--] = '\0';
    return mem;
}