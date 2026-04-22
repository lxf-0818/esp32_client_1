# login.cpp

## Purpose
The login module handles startup credentials for the ESP32 client:
- reads Blynk auth token from LittleFS
- reads AES key and IV from LittleFS
- decrypts stored WiFi credentials
- reads API key data from LittleFS

## Files Used from LittleFS
- /blynkAuth.txt: Blynk token string
- /aes.txt: comma-separated AES key bytes in hex
- /iv.txt: comma-separated AES IV bytes in hex
- /ssid_pass_aes.txt: AES-encrypted SSID:password string
- /api.txt: API key used by the client

## Main API

### decryptWifiCredentials(auth, ssid, pass)
Mounts LittleFS, loads crypto material, decrypts `SSID:PASSWORD`, and writes results into output buffers.

Current return behavior in code:
- returns `0` after normal function flow
- may restart the device if LittleFS cannot be mounted

Notes:
- if a file cannot be opened, helper functions log an error and return empty/default data
- parsing assumes decrypted payload contains `:` between SSID and password

### aes_init()
Sets AES padding mode and initializes working IV copies.

### encrypt_to_ciphertext(msg, iv)
Encrypts plaintext to Base64 ciphertext and runs an internal decrypt check.

### decrypt_to_cleartext(msg, len, iv, cleartext)
Decrypts Base64 ciphertext and truncates at the first non-printable byte (`< 32`) to keep a valid C-string.

### readAES(fileName, data)
Parses comma-separated hex bytes into a byte buffer.

### readLittle(fileName)
Reads a text file from LittleFS and returns it as a String.

## Data Format Requirements
- AES key and IV files must be hex byte lists, for example:
  `2B,7E,15,16,...`
- decrypted WiFi format must be exactly:
  `SSID:PASSWORD`

## Failure Modes
- LittleFS mount failure: device restart (`ESP.restart()`)
- missing/invalid files: helper reads may return empty data and logs to Serial
- malformed key/iv input: can produce invalid decrypt output
- missing `:` in decrypted credentials: SSID/password parsing becomes invalid

## Integration Points
Used in startup flow in `src/main.cpp` to initialize:
- Blynk auth token
- WiFi SSID/password
- API key (`phpKey`)
