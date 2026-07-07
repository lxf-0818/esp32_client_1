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

#define INPUT_BUFFER_LIMIT 2048
#define SOCKET_AES
#define MAX_LINE_LENGTH 120
#define PORT 8888
#define DEVICES 8
// #define DEBUG

extern int failSocket, passSocket, recoveredSocket, retry;
extern byte enc_iv_copy[N_BLOCK], aes_iv[N_BLOCK];
extern char cleartext[INPUT_BUFFER_LIMIT];

void taskSQL_HTTP(void *pvParameters);
int socketClient(char *espServer, char *command);
char * socketClient(char *espServer, String command);
void upDateWidget(char *sensor, float tokens[]);
void printTokens(float tokens[DEVICES][5]);
void decrypt_to_cleartext(char *msg, uint16_t msgLen, byte iv[], char *cleartext);
bool queStat();

/**
 * @brief Establishes a socket connection to a server, sends a command, and processes the response.
 *
 * @param espServer A pointer to a character array containing the server address.
 * @param command A pointer to a character array containing the command to send to the server.
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
 * @warning Ensure that the server address and command strings are properly null-terminated.
 * @note Response timeout is 5 seconds in the `client.available()` wait loop.
 *
 */
int socketClient(char *espServer, char *command)
{
    extern float tokens[DEVICES][5];
    char str[500];
    bzero(str, 500);
    WiFiClient client;
    CRC32 crc;

    if (!client.connect(espServer, PORT))
    {
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
// #define DEBUG_TOKENS
#ifdef DEBUG_TOKENS
    printTokens(tokens);
#endif

    return 0;
}

/**
 * @brief Prints parsed sensor token rows to the Serial monitor.
 *
 * This function iterates through up to 5 rows of the provided token matrix and
 * prints each row to the Serial monitor. The first element of each row is
 * treated as a sensor ID and is printed in hexadecimal format, while the
 * remaining elements are printed as floating-point numbers. The function
 * stops processing rows when the first element of a row is zero.
 *
 * @param tokens A `DEVICES x 5` matrix of floating-point token values.
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
/**
 * @brief Establishes a socket connection to a server, sends a command, and retrieves the response.
 *
 * @param espServer A pointer to a character array containing the server's address.
 * @param command A String containing the command to send to the server.
 * @return A pointer to a dynamically allocated character array containing the server's response.
 *         Returns NULL if the connection fails, a timeout occurs, or memory allocation fails.
 *
 * @note The caller is responsible for freeing the memory allocated for the response using free().
 *
 * @details
 * - The function attempts to connect to the specified server using the WiFiClient class.
 * - If the connection is successful, it sends the provided command to the server.
 * - The function waits for a response from the server, with a timeout of 10 seconds.
 * - If no response is received within the timeout period, the connection is closed, and NULL is returned.
 * - The response from the server is read into a dynamically allocated buffer (80 bytes).
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
char *socketClient(char *espServer, const String &command)
{
    int j = 0;
    WiFiClient client;
    if (!client.connect(espServer, PORT))
    {
        Serial.print("connection failed from socketClient ");
        Serial.println(espServer);
        return NULL;
    }
    if (client.connected())
        client.println(command); // send cmd to server (esp8266) ie "BLK"/"RST"

    char *mem = (char *)malloc(80);
    if (mem == NULL)
    {
        //  did you call free()?
        // Blynk.logEvent("mem_alloc_failed");
        queStat();
        ESP.restart();
    }
    // RST is fire-and-forget: server reboots immediately, so no response payload is expected.
    // Return a user-facing status string to the caller and close the socket right away.
    if (command == "RST")
    {
        client.stop();
        sprintf(mem, "Server %s Was Rebooted", espServer);
        return mem;
    }
    unsigned long timeout = millis();
    // wait for data to be available
    while (client.available() == 0)
    {
        if (millis() - timeout > 10000)
        {
            Serial.println(">>> Client Timeout !");
            client.stop();
            return NULL;
        }
    }
    // read sensor data from sever
    while (client.available())
    { // read data from server (esp8266)
        char ch = static_cast<char>(client.read());
        mem[j++] = ch;
    }
    // Close the connection
    client.stop();
    mem[j--] = '\0';
    return mem;
}