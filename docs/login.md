# Login and AES Module Documentation

This document describes the behavior of src/login.cpp.

## Purpose

The module handles encrypted credential storage and runtime decryption for the ESP32 client:
- Initializes AES settings
- Encrypts and decrypts strings with AESLib
- Reads AES key and IV from LittleFS
- Loads Blynk auth token and decrypts Wi-Fi credentials from filesystem files

## Dependencies

- Arduino core: Arduino.h
- Network: WiFi.h
- Crypto: AESLib.h
- Filesystem: LittleFS.h
- Misc: time.h

## Constants and Buffers

- INPUT_BUFFER_LIMIT = 2048
  - Maximum plaintext buffer size used for cleartext handling.

Global buffers:
- cleartext[INPUT_BUFFER_LIMIT]
- ciphertext[2 * INPUT_BUFFER_LIMIT]

Global key/IV state:
- aes_key[N_BLOCK]
- aes_iv[N_BLOCK]
- enc_iv_to[N_BLOCK]
- enc_iv_from[N_BLOCK]

## Initialization

### void aes_init()

Behavior:
- Sets AES padding mode to 0 through AESLib.
- Copies aes_iv into both working IV buffers used for encrypt/decrypt operations.

Notes:
- Must run after key and IV are loaded when valid IV data is required.

## Encryption and Decryption API

### uint16_t encrypt_to_ciphertext(char *msg, byte iv[])

Behavior:
- Encrypts null-terminated plaintext in msg using AESLib.encrypt64.
- Writes Base64 ciphertext into global ciphertext buffer.
- Performs immediate decrypt self-check and prints match when round-trip equals original input.

Returns:
- Encrypted output length returned by AESLib.

### void encrypt_stub(char *str, char *aes_encrypt)

Behavior:
- Resets encrypt IV copy.
- Encrypts str and copies global ciphertext into output buffer aes_encrypt.

### void decrypt_to_cleartext(char *msg, uint16_t msgLen, byte iv[], char *cleartext)

Behavior:
- Decrypts Base64 encrypted input into cleartext.
- Scans decrypted bytes and truncates at first non-printable ASCII byte (< 32) by inserting null terminator.

Notes:
- Uses caller-provided IV buffer, which should be a fresh copy for deterministic CBC behavior.

## Credentials Flow

### int decryptWifiCredentials(char *auth, char *ssid, char *pass)

Filesystem inputs expected in LittleFS root:
- /blynkAuth.txt
- /aes.txt
- /iv.txt
- /ssid_pass_aes.txt

Behavior:
1. Mounts LittleFS.
2. Reads Blynk token from /blynkAuth.txt into auth.
3. Loads AES key and IV using readAES.
4. Reads encrypted SSID:password payload from /ssid_pass_aes.txt.
5. Decrypts payload and splits on first colon.
6. Writes SSID and password into output buffers.

Returns:
- 0 on success
- 2 if /ssid_pass_aes.txt cannot be opened

Failure behavior:
- Restarts MCU when filesystem mount fails.
- Restarts MCU when /blynkAuth.txt cannot be opened.

## Key and IV Parsing

### int readAES(char *fileName, byte data[])

Behavior:
- Opens a file containing comma-separated hex bytes.
- Parses tokens using sscanf("%x") and stores bytes into data[].

Expected file format example:
- 2b,7e,15,16,28,ae,d2,a6,ab,f7,15,88,09,cf,4f,3c

Returns:
- 0 on success
- 2 when file open fails

## Data and File Contract

- /aes.txt and /iv.txt must provide enough comma-separated hex entries for N_BLOCK bytes.
- /ssid_pass_aes.txt must contain valid AESLib Base64 ciphertext of plain string SSID:PASS.
- Output buffers auth, ssid, and pass must be pre-allocated by caller.

## Operational Notes

- The module uses global crypto buffers, so calls are not re-entrant.
- Several copies use strcpy and assume destination buffer is large enough.
- readAES tokenization uses strtok on key.c_str() cast to char*, which relies on mutable String storage behavior and is fragile.

## Suggested Hardening

- Replace strcpy with bounded copies.
- Validate split index before substring extraction in decryptWifiCredentials.
- Parse AES data from a mutable char buffer instead of casting key.c_str().
- Introduce explicit buffer length parameters for public functions.
- Close all opened files explicitly after reads for clarity and resource safety.
