/**
 * @file login.cpp
 * @brief This file contains functions for AES encryption and decryption,
 *        as well as handling WiFi credentials securely.
 *
 * Includes:
 * - AES encryption and decryption using the AESLib library.
 * - File system operations using LittleFS to store and retrieve encrypted WiFi credentials.
 * - Utility functions for encryption, decryption, and initialization.
 *
 * Dependencies:
 * - Arduino.h
 * - time.h
 * - WiFi.h
 * - AESLib.h
 * - LittleFS.h
 *
 * Constants:
 * - INPUT_BUFFER_LIMIT: The maximum size of the input buffer for encryption/decryption.
 *
 * Global Variables:
 * - aesLib: Instance of AESLib for encryption and decryption.
 * - aes_key: The AES encryption key.
 * - aes_iv: The AES initialization vector.
 * - enc_iv_to: A copy of the initialization vector for encryption.
 * - enc_iv_from: A copy of the initialization vector for decryption.
 * - cleartext: Buffer for storing decrypted text.
 * - ciphertext: Buffer for storing encrypted text in Base64 format.
 *
 *
 * Usage:
 * - Call aes_init() to initialize the AES library before using encryption or decryption functions.
 * - Use decryptWifiCredentials() to retrieve and decrypt WiFi credentials stored in a file.
 * - Use encrypt_stub() or encrypt_to_ciphertext() to encrypt data.
 * - Use decrypt_to_cleartext() to decrypt data.
 */
#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
#include <AESLib.h>
#include <LittleFS.h>
#define INPUT_BUFFER_LIMIT 2048
AESLib aesLib;
byte aes_key[] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};
byte aes_iv[N_BLOCK] = {0x05, 0x18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
byte enc_iv_to[N_BLOCK] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
byte enc_iv_from[N_BLOCK] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
char cleartext[INPUT_BUFFER_LIMIT] = {0};      // THIS IS INPUT BUFFER (FOR TEXT)
char ciphertext[2 * INPUT_BUFFER_LIMIT] = {0}; // THIS IS OUTPUT BUFFER (FOR BASE64-ENCODED ENCRYPTED DATA)

void aes_init();
uint16_t encrypt_to_ciphertext(char *msg, byte iv[]);
void encrypt_stub(char *str, char *str2);
void decrypt_to_cleartext(char *msg, uint16_t msgLen, byte iv[], char *cleartext);
int decryptWifiCredentials(char *ssid, char *psw);

void aes_init()
{
  // aesLib.gen_iv(aes_iv);
  aesLib.set_paddingmode((paddingMode)0);
  memcpy(enc_iv_to, aes_iv, sizeof(aes_iv));
  memcpy(enc_iv_from, aes_iv, sizeof(aes_iv));
}

/**
 * @brief Encrypts a plaintext message into ciphertext using AES encryption.
 *
 * This function takes a plaintext message and an initialization vector (IV),
 * encrypts the message using AES encryption, and returns the length of the
 * encrypted ciphertext. It also performs a test decryption to ensure the
 * encryption and decryption processes are functioning correctly.
 *
 * @param msg A pointer to the plaintext message to be encrypted. The message
 *            must be null-terminated.
 * @param iv  A byte array representing the initialization vector (IV) used
 *            for encryption. The IV must be the correct size for the AES
 *            encryption algorithm.
 *
 * @return The length of the encrypted ciphertext.
 */
uint16_t encrypt_to_ciphertext(char *msg, byte iv[])
{
  int msgLen = strlen(msg);
  int cipherlength = aesLib.get_cipher64_length(msgLen);
  char encrypted_bytes[cipherlength];
  uint16_t enc_length = aesLib.encrypt64((byte *)msg, msgLen, encrypted_bytes, aes_key, sizeof(aes_key), iv);

  // test aes encrypt/decrypt to ensure we good to go
  sprintf(ciphertext, "%s", encrypted_bytes);
  memcpy(enc_iv_to, aes_iv, sizeof(aes_iv));
  decrypt_to_cleartext(ciphertext, strlen(ciphertext), enc_iv_to, cleartext);
  // Serial.printf("decrypt str %s\n", cleartext);

  if (!strcmp(cleartext, msg))
    Serial.println("match");
  return enc_length;
}
void encrypt_stub(char *str, char *aes_encrypt)
{
  memcpy(enc_iv_to, aes_iv, sizeof(aes_iv));
  encrypt_to_ciphertext(str, enc_iv_to);
  strcpy(aes_encrypt, ciphertext);
#ifdef DEBUG
  Serial.printf("clear text      %s\n", str);
  Serial.printf("encrypt string: %s\n", ciphertext);
#endif
}
/**
 * @brief Decrypts a base64-encoded encrypted message into cleartext.
 *
 * This function takes an encrypted message, decrypts it using AES encryption,
 * and stores the resulting cleartext in the provided buffer. Any non-printable
 * ASCII characters (below 32) in the cleartext are replaced with '\0' to terminate
 * the string.
 *
 * @param msg       Pointer to the base64-encoded encrypted message.
 * @param msgLen    Length of the encrypted message.
 * @param iv        Initialization vector (IV) used for decryption.
 * @param cleartext Pointer to the buffer where the decrypted cleartext will be stored.
 *                  The buffer must be large enough to hold the decrypted data.
 *
 * @note On ESP8266, the function logs the free heap memory before decryption.
 * @note If DEBUG is defined, additional debug information is printed to the Serial monitor.
 */
void decrypt_to_cleartext(char *msg, uint16_t msgLen, byte iv[], char *cleartext)
{
#ifdef ESP8266
  // Serial.print("[decrypt_to_cleartext] free heap: ");
  ESP.getFreeHeap();
#endif
  uint16_t decLen = aesLib.decrypt64(msg, msgLen, (byte *)cleartext, aes_key, sizeof(aes_key), iv);

  for (int j = 0; j < decLen; j++)
  {
    // Replace any non-printable ASCII characters (below 32) with '\0' to terminate the string.

    if (cleartext[j] < 32)
    {
      cleartext[j] = '\0'; // null-terminated string
#ifdef DEBUG
      Serial.printf("break j=%d len =%d \n", j, decLen);
#endif
      break;
    }
  }
}

int decryptWifiCredentials(char *ssid, char *pass)
{
  String ssid_psw_aes;
  bool success = LittleFS.begin();
  if (!success)
  {
    Serial.println("Error mounting the file system");
    return 1;
  }
  File file = LittleFS.open("/ssid_pass_aes.txt", "r");
  if (!file)
  {
    Serial.println("Failed to open ssid_pass_aes.txt file for reading");
    return 2;
  }
  ssid_psw_aes.clear();
  while (file.available())
    ssid_psw_aes.concat(static_cast<char>(file.read()));

  file.close();

  decrypt_to_cleartext((char *)ssid_psw_aes.c_str(), ssid_psw_aes.length(), aes_iv, cleartext);
  String temp = cleartext;
  int index = temp.indexOf(":");
  strcpy(ssid, (temp.substring(0, index)).c_str());
  strcpy(pass, (temp.substring(index + 1)).c_str());

  return 0;
}
