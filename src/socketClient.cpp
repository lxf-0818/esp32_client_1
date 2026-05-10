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
 * - The `SOCKET_AES` macro enables AES decryption for socket communication.
 * - The `DEBUG_TOKENS` macro enables debug output for token printing via `printTokens()`.
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
#include <AESLib.h>

#define NO_UPDATE_FAIL 0
#define INPUT_BUFFER_LIMIT 2048
#define SOCKET_AES
#define MAX_LINE_LENGTH 120
#define PORT 8888
#define DEVICES 6
// #define DEBUG

extern String lastMsg;
extern int failSocket, passSocket, recoveredSocket, retry;
extern byte enc_iv_copy[N_BLOCK], aes_iv[N_BLOCK];
extern char cleartext[INPUT_BUFFER_LIMIT];
void taskSQL_HTTP(void *pvParameters);
void setupHTTP_request(String sensorName, float tokens[]);
int socketRecovery(char *IP, char *cmd2Send);
int socketClient(char *espServer, char *command, bool updateErrorQueue);
void upDateWidget(char *sensor, float tokens[]);
void processSensorData(float tokens[DEVICES][5]);
void printTokens(float tokens[DEVICES][5]);
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
 * 4. Reads the response into a local buffer.
 * 5. Validates the CRC32 over the ciphertext substring.
 * 6. Decrypts the payload with AES-128-CBC when SOCKET_AES is defined.
 * 7. Tokenizes the plaintext records into the global `tokens[DEVICES][5]` float matrix.
 *
 * On failure the function sets `lastMsg` to a descriptive error string and returns
 * a non-zero code. Recovery actions (calling `socketRecovery()`, incrementing
 * `failSocket`) are the caller's responsibility.
 *
 * @note `updateErrorQueue` is accepted for API symmetry but is currently unused
 *       (`(void)updateErrorQueue`). The parameter is reserved for future use.
 * @note Updates global `lastMsg` on connect failure, timeout, and CRC mismatch.
 *
 * @warning Ensure that the server address and command strings are properly null-terminated.
 *
 */
int socketClient(char *espServer, char *command, bool updateErrorQueue)
{
    (void)updateErrorQueue;
    extern float tokens[DEVICES][5];
    char str[500];
    bzero(str, 500);
    WiFiClient client;
    CRC32 crc;

    if (!client.connect(espServer, PORT))
    {
        Serial.printf(">>> failed to connect: %s\n", espServer);
        lastMsg = "failed to connect " + String(espServer);
        client.stop();
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
            return 2;
        }
    }
    int index = 0, index1 = 0, calculatedCrc;
    while (client.available())
        str[index++] = client.read(); // read sensor data from sever
    client.stop();
// #define TRACE
#ifdef TRACE
    Serial.printf("conected to %s received %s\n", espServer, str);
#endif

    String copyStr = str;
    index = copyStr.indexOf(":");
    index1 = copyStr.lastIndexOf(":");
    String crcString = copyStr.substring(0, index);
    sscanf(crcString.c_str(), "%x", &calculatedCrc); // convert ASCII string to hex 0xYY

    String parsed = copyStr.substring(index + 1, index1);
    crc.add((uint8_t *)parsed.c_str(), parsed.length());
    if (calculatedCrc != crc.calc())
    {
        lastMsg = "CRC invalid " + String(espServer);
        client.stop();
        return 3;
    }
    byte new_iv[16];
    int i = 0, iv_tmp;

    // AES Initialization Vector (IV) is a random, non-secret value used to ensure that encrypting the
    // same plaintext with the same key produces unique ciphertext, preventing pattern recognition.
    String IV = copyStr.substring(index1 + 1);
    char *token1 = strtok((char *)IV.c_str(), ",");
    while (token1 != NULL)
    {
        sscanf(token1, "%x", &iv_tmp); // convert ASCII string to hex 0xYY
        new_iv[i++] = iv_tmp;
        token1 = strtok(NULL, ",");
    }

#ifdef SOCKET_AES
    memcpy(enc_iv_copy, new_iv, sizeof(new_iv)); // since new iv is gen every i/o might not need?
    decrypt_to_cleartext((char *)parsed.c_str(), parsed.length(), enc_iv_copy, cleartext);
    parsed = String(cleartext);
#endif
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
//#define DEBUG_TOKENS
#ifdef DEBUG_TOKENS
    printTokens(tokens);
#endif

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
 * @note A previous bug related to "Stack canary" exceptions was resolved by increasing the stack size.
 */
void processSensorData(float tokens[DEVICES][5])
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
void printTokens(float tokens[DEVICES][5])
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