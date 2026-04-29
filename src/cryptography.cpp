/**
 * @file cryptography.cpp
 * @brief AES-128-CBC encrypt/decrypt helpers and LittleFS credential utilities.
 *
 * Provides:
 *  - AES library initialisation (zero-padding mode)
 *  - High-level encrypt/decrypt wrappers that protect the master key/IV from
 *    in-place mutation by AESLib
 *  - LittleFS helpers to read comma-separated hex key files and plain-text files
 *  - Boot-time credential decryption: Wi-Fi SSID/password and Blynk auth token
 *
 * All AES operations use a 128-bit key (N_BLOCK = 16 bytes).
 * The IV is treated as a single-use value per encrypt call; a scratch copy
 * (`enc_iv_copy`) is passed to AESLib so the master `aes_iv` is not mutated.
 */
#include <Arduino.h>
#include <AESLib.h>
#include <LittleFS.h>
#define INPUT_BUFFER_LIMIT 2048
AESLib aeslib;
byte aes_key[N_BLOCK];      ///< Master AES-128 key — loaded from /aes.txt, never passed directly to AESLib
byte aes_key_copy[N_BLOCK]; ///< Scratch copy of aes_key passed to AESLib (mutated in-place by each call)
byte aes_iv[N_BLOCK];       ///< Master AES IV — loaded from /iv.txt, never passed directly to AESLib
byte enc_iv_copy[N_BLOCK];  ///< Scratch copy of aes_iv passed to AESLib (mutated in-place by each call)
char cleartext[INPUT_BUFFER_LIMIT] = {0};      ///< Plaintext workspace (input buffer)
char ciphertext[2 * INPUT_BUFFER_LIMIT] = {0}; ///< Base64-encoded ciphertext workspace (output buffer)
void aes_init();
uint16_t encrypt_to_ciphertext(char *msg, byte iv[]);
void encrypt_stub(char *str, char *str2);
void decrypt_to_cleartext(char *msg, uint16_t msgLen, byte iv[], char *cleartext);
int decryptWifiCredentials(char *auth, char *ssid, char *pass);
int readAES(char *fileName, byte data[]);
String readLittle(char *fileName);
String phpKey;
/**
 * @brief Initialises the AES library with zero-padding mode.
 *
 * Sets the AESLib padding mode to 0 (zero-padding). Must be called once before
 * any encrypt or decrypt operation.
 *
 * @note IV generation is NOT performed here. The master `aes_iv` is loaded
 *       separately by `decryptWifiCredentials()` via `readAES()`.
 */
void aes_init()
{
  // aesLib.gen_iv(aes_iv);
  aeslib.set_paddingmode((paddingMode)0);
}

/**
 * @brief Low-level AES-128-CBC encrypt + base64 encode.
 *
 * Encrypts @p msg into the global `ciphertext[]` buffer, then performs a
 * round-trip decrypt to verify correctness (prints "match" to Serial on
 * success).
 *
 * @param msg Pointer to the null-terminated plaintext to encrypt.
 * @param iv  Scratch IV buffer — consumed (mutated) by AESLib. Always pass
 *            a copy of `aes_iv`, never the master buffer directly.
 *
 * @return Length of the base64-encoded ciphertext written to `ciphertext[]`.
 *
 * @warning `iv` is mutated in-place. Use `enc_iv_copy` (copied from `aes_iv`)
 *          rather than `aes_iv` itself.
 */
uint16_t encrypt_to_ciphertext(char *msg, byte iv[])
{
  int msgLen = strlen(msg);
  int cipherlength = aeslib.get_cipher64_length(msgLen);
  char encrypted_bytes[cipherlength];
  uint16_t enc_length = aeslib.encrypt64((byte *)msg, msgLen, encrypted_bytes, aes_key, sizeof(aes_key), iv);

  // test aes encrypt/decrypt to ensure we good to go
  sprintf(ciphertext, "%s", encrypted_bytes);
  memcpy(enc_iv_copy, aes_iv, sizeof(aes_iv));
  decrypt_to_cleartext(ciphertext, strlen(ciphertext), enc_iv_copy, cleartext);
  // Serial.printf("decrypt str %s\n", cleartext);

  if (!strcmp(cleartext, msg))
    Serial.println("match");
  return enc_length;
}
/**
 * @brief High-level encrypt entry point.
 *
 * Copies the master IV into the scratch buffer `enc_iv_copy`, calls
 * `encrypt_to_ciphertext()`, then copies the result from the global
 * `ciphertext[]` into @p aes_encrypt.
 *
 * @param str         Pointer to the null-terminated plaintext to encrypt.
 * @param aes_encrypt Output buffer for the base64-encoded ciphertext.
 *                    Must be at least `2 * INPUT_BUFFER_LIMIT` (4096) bytes.
 */
void encrypt_stub(char *str, char *aes_encrypt)
{
  memcpy(enc_iv_copy, aes_iv, sizeof(aes_iv));
  encrypt_to_ciphertext(str, enc_iv_copy);
  strcpy(aes_encrypt, ciphertext);
#ifdef DEBUG
  Serial.printf("clear text      %s\n", str);
  Serial.printf("encrypt string: %s\n", ciphertext);
#endif
}
/**
 * @brief AES-128-CBC decrypt + base64 decode.
 *
 * Decrypts @p msg into @p cleartext and null-terminates the result by
 * replacing the first non-printable ASCII character (value < 32) with '\0'.
 *
 * @param msg       Pointer to the base64-encoded ciphertext.
 * @param msgLen    Length of @p msg in bytes.
 * @param iv        Scratch IV buffer — consumed (mutated) by AESLib. Always
 *                  pass a copy of `aes_iv`, never the master buffer directly.
 * @param cleartext Output buffer for the decrypted plaintext. Must be large
 *                  enough to hold the decrypted data (at most `msgLen` bytes).
 *
 * @note On ESP8266 builds, `ESP.getFreeHeap()` is called as a heap diagnostic
 *       (result discarded; guarded by `#ifdef ESP8266`).
 * @note When `DEBUG` is defined, the raw ciphertext and resulting plaintext
 *       are printed to the Serial monitor.
 */
void decrypt_to_cleartext(char *msg, uint16_t msgLen, byte iv[], char *cleartext)
{
//#define DEBUG
#ifdef ESP8266
  // Serial.print("[decrypt_to_cleartext] free heap: ");
  ESP.getFreeHeap();
#endif
  uint16_t decLen = aeslib.decrypt64(msg, msgLen, (byte *)cleartext, aes_key, sizeof(aes_key), iv);

  for (int j = 0; j < decLen; j++)
  {
    // Replace 1st  non-printable ASCII characters (below 32) with '\0' to terminate the string.

    if (cleartext[j] < 32)
    {
      cleartext[j] = '\0'; // null-terminated string
#ifdef DEBUG
      Serial.printf("break j=%d len =%d \n", j, decLen);
#endif
      break;
    }
  }
#ifdef DEBUG
  Serial.printf("Encrypt %s  ClearTxt %s \n", msg, cleartext);
#endif
}

/**
 * @brief Mounts LittleFS and decrypts stored Wi-Fi credentials and Blynk auth token.
 *
 * Reads all required key material and credential blobs from LittleFS, decrypts
 * the Wi-Fi SSID/password pair, and copies results into the caller's buffers.
 *
 * Files read:
 *  - `/aes.txt`          — comma-separated hex bytes of the 16-byte AES key
 *  - `/iv.txt`           — comma-separated hex bytes of the 16-byte IV
 *  - `/api.txt`          — plaintext PHP API key (stored in global `phpKey`)
 *  - `/ssid_pass_aes.txt`— AES-CBC-encrypted, base64-encoded "SSID:PASSWORD" blob
 *  - `/blynkAuth.txt`    — plaintext Blynk authentication token
 *
 * @param auth Output buffer for the Blynk authentication token.
 * @param ssid Output buffer for the decrypted Wi-Fi SSID.
 * @param pass Output buffer for the decrypted Wi-Fi password.
 *
 * @return 0 on success. Does not return on LittleFS mount failure
 *         (`ESP.restart()` is called instead).
 *
 * @warning Caller is responsible for ensuring @p auth, @p ssid, and @p pass
 *          are large enough to hold their respective strings.
 * @note `aes_iv` is protected from AESLib mutation by copying it into
 *       `enc_iv_copy` before the decrypt call.
 */
int decryptWifiCredentials(char *auth, char *ssid, char *pass)
{
  String ssid_psw_aes, tmp;

  bool success = LittleFS.begin();
  if (!success)
  {
    Serial.println("Error mounting the file system");
    ESP.restart();
  }

  readAES((char *)"/aes.txt", aes_key);
  readAES((char *)"/iv.txt", aes_iv);
  phpKey = readLittle((char *)"/api.txt");
  ssid_psw_aes = readLittle((char *)"/ssid_pass_aes.txt");
  String blyAuth = readLittle((char *)"/blynkAuth.txt");
  strcpy(auth, blyAuth.c_str());

  // save a copy decrypt_to_cleartext() corrupts byte array aes_iv!
  memcpy(enc_iv_copy, aes_iv, sizeof(aes_iv));
  decrypt_to_cleartext((char *)ssid_psw_aes.c_str(), ssid_psw_aes.length(), enc_iv_copy, cleartext);
  String temp = cleartext;
  int index = temp.indexOf(":");
  strcpy(ssid, (temp.substring(0, index)).c_str());
  strcpy(pass, (temp.substring(index + 1)).c_str());

  return 0;
}




/**
 * @brief Reads the full contents of a LittleFS file into an Arduino String.
 *
 * @param fileName Path to the file on LittleFS (e.g. "/api.txt").
 * @return The file contents as a String, or an empty String if the file
 *         could not be opened (an error is printed to Serial).
 */
String readLittle(char *fileName)
{
  File file = LittleFS.open(fileName, "r");
  if (!file)
  {
    Serial.printf("Failed to open %s file for reading\n", fileName);
    return "";
  }
  String returnString;
  while (file.available())
    returnString.concat(static_cast<char>(file.read()));

  file.close();

  return returnString;
}
/**
 * @brief Reads a comma-separated ASCII hex file from LittleFS into a byte array.
 *
 * Parses lines of the form `a1,b2,c3,...` and stores each converted byte
 * sequentially into @p data[]. Caller must ensure @p data is large enough
 * (typically N_BLOCK = 16 bytes for key/IV files).
 *
 * @param fileName Path to the file on LittleFS (e.g. "/aes.txt", "/iv.txt").
 * @param data     Output byte array to receive the parsed values.
 *
 * @return 0 on success, 2 if the file could not be opened.
 */
int readAES(char *fileName, byte data[])
{
  File file = LittleFS.open(fileName, "r");
  if (!file)
  {
    Serial.printf("Failed to open %s file for reading\n",fileName);
    return 2;
  }
  String tmp;
  while (file.available())
    tmp.concat(static_cast<char>(file.read()));

  int foo, i = 0;
  char *token = strtok((char *)tmp.c_str(), ",");
  while (token != NULL)
  {
    sscanf(token, "%x", &foo); // convert ASCII string to hex 0xYY
    data[i++] = foo;
    token = strtok(NULL, ",");
  }
  file.close();
  return 0;
}
