# login.cpp

## Purpose
The login module handles secure startup credentials for the ESP32 client:
- reads Blynk auth token from LittleFS
- reads AES key and IV from files
- decrypts WiFi credentials from encrypted storage

## Files Used from LittleFS
- /blynkAuth.txt: Blynk token string
- /aes.txt: comma-separated AES key bytes in hex
- /iv.txt: comma-separated AES IV bytes in hex
- /ssid_pass_aes.txt: AES-encrypted SSID:password string

## Main API

### decryptWifiCredentials(auth, ssid, pass)
Reads all required files, decrypts SSID/password, and returns:
- 0 on success
- 2 if encrypted credential file cannot be opened

Behavior details:
- mounts LittleFS
- restarts ESP32 on critical missing files (blynkAuth or fs mount failure)
- parses decrypted payload by splitting on ':'

### aes_init()
Sets AES padding mode and initializes working IV copies.

### encrypt_to_ciphertext(msg, iv)
Encrypts plaintext into base64 ciphertext and performs round-trip decrypt verification.

### decrypt_to_cleartext(msg, len, iv, cleartext)
Decrypts base64 ciphertext and truncates at first non-printable byte (<32) to force valid C-string termination.

### readAES(fileName, data)
Parses comma-separated hex bytes into a byte buffer.

## Data Format Requirements
- AES key and IV files must be hex byte lists, for example:
  `2B,7E,15,16,...`
- decrypted WiFi format must be exactly:
  `SSID:PASSWORD`

## Failure Modes
- LittleFS mount failure: device restart
- missing blynkAuth file: device restart
- missing ssid_pass_aes file: returns error code 2
- malformed key/iv input: can produce invalid decrypt output

## Integration Points
Used by startup path in src/main.cpp to initialize:
- Blynk auth token
- WiFi SSID/password

Without successful decrypt flow, setup restarts the ESP32.
