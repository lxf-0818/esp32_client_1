# cryptography.cpp

Last updated: 2026-07-31

## Purpose
Provides AES-128-CBC encrypt/decrypt helpers and LittleFS I/O utilities used across the ESP32 client firmware:
- initialize the AES library in zero-padding mode
- encrypt plaintext to base64-encoded ciphertext
- decrypt base64-encoded ciphertext to plaintext
- read comma-separated hex key/IV files from LittleFS
- mount LittleFS, decrypt stored Wi-Fi credentials, retrieve the Blynk auth token, and load the backend base URL

## Global buffers

| Symbol | Size | Role |
|---|---|---|
| aes_key[N_BLOCK] | 16 bytes | Active AES-128 key loaded from /aes.txt |
| aes_key_copy[N_BLOCK] | 16 bytes | Scratch key buffer retained for AES operations |
| aes_iv[N_BLOCK] | 16 bytes | Active AES IV loaded from /iv.txt |
| enc_iv_copy[N_BLOCK] | 16 bytes | Scratch IV used for both encrypt and decrypt |
| cleartext[] | 2048 bytes | Plaintext workspace |
| ciphertext[] | 4096 bytes | Base64-encoded ciphertext output workspace |
| phpKey | String | PHP API key read from /api.txt |
| phpServerIP | String | Backend base URL prefix read from /phpServerIP.txt |

AESLib mutates the IV in-place on every call, so the scratch buffer is copied from aes_iv before each operation.

## Filesystem inputs

| File | Contents |
|---|---|
| /aes.txt | comma-separated ASCII hex bytes of the 16-byte AES-128 key |
| /iv.txt | comma-separated ASCII hex bytes of the 16-byte IV |
| /ssid_pass_aes.txt | AES-CBC-encrypted, base64-encoded SSID:PASSWORD blob |
| /blynkAuth.txt | plaintext Blynk authentication token |
| /api.txt | plaintext PHP API key |
| /phpServerIP.txt | plaintext backend base URL prefix |

## Key APIs

### aes_init()
Sets the AESLib padding mode to zero-padding. This must be called before encrypting or decrypting.

### encrypt_stub(str, aes_encrypt)
High-level encrypt entry point:
1. copies aes_iv to enc_iv_copy
2. calls encrypt_to_ciphertext()
3. copies the resulting ciphertext into aes_encrypt

### encrypt_to_ciphertext(msg, iv)
Low-level AES-128-CBC encrypt + base64 encode. It calls aeslib.encrypt64(), writes the output to ciphertext[], and performs a round-trip decrypt to verify correctness.

### decrypt_to_cleartext(msg, msgLen, iv, cleartext)
AES-128-CBC decrypt + base64 decode. It calls aeslib.decrypt64() and null-terminates the result at the first non-printable character.

### decryptWifiCredentials(auth, ssid, pass)
Mounts LittleFS and decrypts the stored Wi-Fi credentials and Blynk auth token.

The function:
1. mounts LittleFS and restarts on failure
2. reads aes.txt and iv.txt with readAES()
3. reads /api.txt into phpKey
4. reads /ssid_pass_aes.txt and /blynkAuth.txt
5. reads /phpServerIP.txt into phpServerIP
6. decrypts the Wi-Fi SSID/password pair into the caller buffers

## LittleFS helpers

### readAES(fileName, data[])
Opens a comma-separated ASCII hex file and stores each parsed byte in the supplied byte array.

### readLittle(fileName)
Reads the full contents of a LittleFS file into an Arduino String and returns it.

## Compile flags
- ESP8266: enables the heap diagnostic call inside decrypt_to_cleartext()
- DEBUG: enables verbose Serial output in the encrypt/decrypt helpers
