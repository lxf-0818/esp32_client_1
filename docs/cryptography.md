# cryptography.cpp

## Purpose
Provides AES-128-CBC encrypt/decrypt helpers and LittleFS I/O utilities used
across the ESP32 client firmware:
- initialise AES library (padding mode)
- encrypt plaintext to base64-encoded ciphertext
- decrypt base64-encoded ciphertext to plaintext
- read and parse comma-separated hex key/IV files from LittleFS
- mount LittleFS, decrypt stored Wi-Fi credentials, and retrieve the Blynk auth token

## Global Buffers

| Symbol | Size | Role |
|---|---|---|
| `aes_key[N_BLOCK]` | 16 bytes | Active AES-128 key loaded from `/aes.txt` |
| `aes_iv[N_BLOCK]` | 16 bytes | Active AES IV loaded from `/iv.txt` |
| `enc_iv_to[N_BLOCK]` | 16 bytes | Scratch IV used when encrypting (copy of `aes_iv`, mutated by AESLib) |
| `enc_iv_from[N_BLOCK]` | 16 bytes | Scratch IV used when decrypting (copy of `aes_iv`, mutated by AESLib) |
| `cleartext[]` | 2048 bytes | Plaintext workspace |
| `ciphertext[]` | 4096 bytes | Base64-encoded ciphertext output workspace |
| `phpKey` | `String` | PHP API key read from `/api.txt` |

> AESLib mutates the IV in-place on every call. The `enc_iv_to` / `enc_iv_from`
> scratch buffers protect the original `aes_iv` by passing copies to each operation.

## Filesystem Inputs

| File | Contents |
|---|---|
| `/aes.txt` | Comma-separated ASCII hex bytes of the 16-byte AES-128 key (e.g. `a1,b2,c3,...`) |
| `/iv.txt` | Comma-separated ASCII hex bytes of the 16-byte IV |
| `/ssid_pass_aes.txt` | AES-CBC-encrypted, base64-encoded `SSID:PASSWORD` blob |
| `/blynkAuth.txt` | Plaintext Blynk authentication token |
| `/api.txt` | Plaintext PHP API key (stored in `phpKey`) |

## Key APIs

### aes_init()
Sets AESLib padding mode to `0` (zero-padding) and copies `aes_iv` into both
`enc_iv_to` and `enc_iv_from`. Must be called before any encrypt or decrypt
operation.

### encrypt_stub(str, aes_encrypt)
High-level encrypt entry point.
1. Copies `aes_iv` → `enc_iv_to`.
2. Calls `encrypt_to_ciphertext(str, enc_iv_to)`.
3. Copies the result from the global `ciphertext[]` into `aes_encrypt`.

`aes_encrypt` must be at least `2 × INPUT_BUFFER_LIMIT` (4096) bytes.

### encrypt_to_ciphertext(msg, iv)
Low-level AES-128-CBC encrypt + base64 encode.
1. Calls `aesLibx.encrypt64()` → result written to global `ciphertext[]`.
2. Performs a round-trip decrypt to verify correctness (`"match"` logged to Serial on success).
3. Returns the ciphertext length.

`iv` is consumed (mutated by AESLib); always pass a copy, not `aes_iv` directly.

### decrypt_to_cleartext(msg, msgLen, iv, cleartext)
AES-128-CBC decrypt + base64 decode.
- Calls `aesLibx.decrypt64()` and null-terminates the result in `cleartext` by
  replacing the first non-printable ASCII character (value < 32) with `'\0'`.
- On ESP8266 builds, `ESP.getFreeHeap()` is called as a heap diagnostic
  (result discarded; guarded by `#ifdef ESP8266`).

`iv` is consumed (mutated); always pass a copy, not `aes_iv` directly.

### decryptWifiCredentials(auth, ssid, pass)
Mounts LittleFS and decrypts stored Wi-Fi credentials and Blynk auth token.

1. Mounts LittleFS; calls `ESP.restart()` on failure.
2. Reads AES key from `/aes.txt` and IV from `/iv.txt` via `readAES()`.
3. Reads PHP API key from `/api.txt` into `phpKey` via `readLittle()`.
4. Reads encrypted credentials from `/ssid_pass_aes.txt` via `readLittle()`.
5. Reads Blynk token from `/blynkAuth.txt` via `readLittle()`; copies into `auth`.
6. Copies `aes_iv` → `enc_iv_to`, calls `decrypt_to_cleartext()`.
7. Splits the resulting `SSID:PASSWORD` string on `:` and copies into `ssid` / `pass`.

Return value:
- `0` — success

Caller is responsible for ensuring `auth`, `ssid`, and `pass` buffers are large
enough to hold their respective strings.

## LittleFS Helpers

### readAES(fileName, data[])
Opens a comma-separated ASCII hex file and stores each parsed byte into `data[]`.

Return value:
- `0` — success
- `2` — file could not be opened

### readLittle(fileName)
Reads the full contents of a LittleFS file and returns them as an Arduino
`String`. Returns an empty `String` and logs an error to Serial if the file
cannot be opened.

## Compile Flags

- `ESP8266` — enables the `ESP.getFreeHeap()` diagnostic call inside
  `decrypt_to_cleartext()`.
- `DEBUG` — enables verbose Serial output in `encrypt_stub()`,
  `encrypt_to_ciphertext()`, and `decrypt_to_cleartext()`.
