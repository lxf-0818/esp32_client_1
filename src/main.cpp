/**
 * @file main.cpp
 * @brief ESP32 client application using Blynk and OLED display for IoT monitoring and control.
 *
 * This program connects an ESP32 to a Wi-Fi network, integrates with the Blynk IoT platform,
 * and communicates with a server to fetch and display device data. It also uses an OLED display
 * to show basic information and implements a watchdog timer for system reliability.
 * @details
 * - The program uses Blynk for IoT communication and virtual pin updates.
 * - It fetches device information from a server and processes it for display and control.
 * - A loop watchdog timer (LWD) is implemented to reboot the system in case of a hang.
 * - The program supports updating Blynk widgets with sensor data and managing device connections.
 *
 * @dependencies
 * - Arduino core for ESP32
 * - Blynk library
 * - Adafruit SSD1306 library for OLED display
 * - HTTPClient for server communication
 * - Ticker for watchdog timer
 *
 * @author Leon Freimour
 * @date 2026-04-30
 *
 * @note The Blynk auth token is stored encrypted in LittleFS (/blynkAuth.txt) and
 *       decrypted at boot by cryptography.cpp. Do not hard-code it in source.
 *
 * @section Functions
 * Functions defined in this file:
 * - setup(): Initializes the system, connects to Wi-Fi, and sets up Blynk and the OLED display.
 * - loop(): Runs the Blynk and timer tasks.
 * - flashSSD(): Renders a startup screen on the OLED display.
 * - checkSSD(): Probes the I2C bus for an SSD1306 OLED at address 0x3C.
 * - refreshWidgets(): Periodically fetches sensor data from the server and updates Blynk widgets.
 * - lwdtcb(): Watchdog timer ISR — restarts the system if the loop hangs.
 * - lwdtFeed(): Resets the loop watchdog heartbeat timestamp.
 * - upDateWidget(): Writes sensor readings to Blynk virtual pins based on sensor type.
 * - performHttpGet(): HTTP GET wrapper; returns response string or empty on failure.
 * - getSensorData(): Parses sensor/IP list from the server and socket-polls each device.
 * - getSensorData4User(): Resolves sensor IPs and reports live readings to the Blynk terminal.
 * - getIP(): Case-insensitive map lookup; returns matching IPs as a '|'-delimited string.
 * - isServerConnected(): TCP reachability check for a given IP and port.
 * - printUptime(): Formats and writes uptime to the Blynk terminal (V49).
 * - ping(): TCP-pings all ipMap entries and HTTP-pings ipList; reports results to V49.
 * - generateInterrupt(): Manually invokes the watchdog ISR for testing.
 * - BLYNK_CONNECTED(): Callback for Blynk connection events.
 * - BLYNK_WRITE(V18): Clears remote IP registrations via HTTP GET.
 * - BLYNK_WRITE(V49): Terminal command parser.
 * - BLYNK_WRITE(BLINK_TST): Sends BLK test command to all known sensor nodes.
 *
 * @section Constants
 * - BLYNK_TEMPLATE_ID, BLYNK_TEMPLATE_NAME: Blynk project identifiers.
 * - SCREEN_WIDTH, SCREEN_HEIGHT: OLED display dimensions (128 x 64 px).
 * - LWD_TIMEOUT: Loop watchdog timeout in milliseconds (15 000 ms).
 * - Server endpoint strings (ipList, ipDelete, getRowCnt, deleteAll, esp_data).
 *
 * @section Notes
 * - Define DEBUG to enable verbose Serial output in selected functions.
 * - Ensure the OLED is wired to the correct I2C pins before enabling the display.
 * - Server endpoint host addresses are hardcoded to 192.168.1.252; update if the server IP changes.
 */
#define BLYNK_TEMPLATE_ID "TMPL21W-vgTej"
#define BLYNK_TEMPLATE_NAME "autoStart"
// #define BLYNK_AUTH_TOKEN moved token to secured  /data

#include <Arduino.h>
#include <map>
#include <FS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <BlynkSimpleEsp32.h>
#include <time.h>
#include <CRC32.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "blynk_widget.h"
#include <Ticker.h>
#include <LittleFS.h>

#define INPUT_BUFFER_LIMIT 2048
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define BLYNK_PRINT Serial

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#define SSD_ADDR 0x3c

void initRTOS();
void flashSSD();
bool checkSSD();

void refreshWidgets();
void getBootTime(char *lastBook, char *strReason);
int getSensorData(const String &sensorsConnected);
void getSensorData4User(String input);
int socketRecovery(char *IP, char *cmd2Send);
void processSensorData(float tokens[5][5]);
String performHttpGet(const char *url);
int decryptWifiCredentials(char *auth, char *ssid, char *psw);
int socketClient(char *espServer, char *command, bool updateErorrQue);
char *socketClient(char *espServer, char *command);
void upDateWidget(char *sensorName, float tokens[]);
void lwdtFeed(void);
void ICACHE_RAM_ATTR lwdtcb(void);
bool queStat();
bool isServerConnected(const char *serverIP, uint16_t port = 8888);
void generateInterrupt();
void printUptime();
String getIP(String sensorName);
void printTokens(float tokens[5][5]);
void ping();

std::map<std::string, std::string> ipMap;
const uint16_t port = 8888;
String sensorName = "NO DEVICE";
int failSocket, passSocket, recoveredSocket, retry, timerID1, passPost;
String sensorsConnected;
HTTPClient http;
String lastMsg;
char lastBoot[20], strReason[60];
BlynkTimer timer;
float tokens[5][5] = {};
bool setAlarm = false;
Ticker lwdTicker;
String lastSensorsConnected = "";
#define LWD_TIMEOUT 15 * 1000 // Reboot if loop watchdog timer reaches this time out value
unsigned long lwdTime = 0;
unsigned long lwdTimeout = LWD_TIMEOUT;
const char *getRowCnt = "http://192.168.1.252/rows.php";
const char *deleteAll = "http://192.168.1.252/deleteALL.php";
const char *ipList = "http://192.168.1.252/ip.php";
const char *ipDelete = "http://192.168.1.252/deleteIP.php";
const char *esp_data = "http://192.168.1.252/esp-data.php";
int myfind(String value);

/**
 * @brief Sets up the initial configuration for the ESP32 client application.
 *
 * This function initializes the serial communication, decrypts Wi-Fi credentials,
 * connects to the Blynk server, checks and flashes the OLED SSD if connected,
 * sets up a timer for refreshing widgets, initializes the RTOS, and configures
 * the lightweight watchdog timer (LWDT).
 *
 * Steps performed:
 * - Initializes serial communication at 115200 baud rate.
 * - Decrypts Wi-Fi credentials and connects to the Blynk server using the provided authentication token.
 * - Checks if the OLED SSD is connected and flashes it if necessary.
 * - Sets up a timer to refresh widgets every 20 seconds.
 * - Initializes the RTOS for multitasking.
 * - Feeds the lightweight watchdog timer to prevent resets.
 * - Attaches a callback routine to the LWDT ticker to handle timeout events.
 */
void setup()
{
  Serial.begin(115200);
  char auth[50];
  char ssid[40], pass[40];
  lastMsg = "no warnings";
  String tmp;
  if (decryptWifiCredentials(auth, ssid, pass))
    ESP.restart();
  Blynk.begin(auth, ssid, pass);

  String IP = WiFi.localIP().toString();
  Serial.printf("IP @: %s\n", IP.c_str());
  if (checkSSD()) //  is OLED SSD connected?
    flashSSD();

  // Serial.println("Turned off timer");
  timerID1 = timer.setInterval(1000L * 20, refreshWidgets); //
  initRTOS();
  lwdtFeed();
  lwdTicker.attach_ms(LWD_TIMEOUT, lwdtcb); // attach lwdt callback routine to Ticker object
  // Blynk.setProperty(V49, "color", "#0fc212ff");
}
/**
 * @brief Main runtime loop for the ESP32 client.
 *
 * Continuously feeds the loop watchdog timer, processes Blynk events, and runs
 * scheduled timer callbacks (including periodic widget refresh tasks).
 */
void loop()
{
  lwdtFeed();
  Blynk.run();
  timer.run();

  // // Example: Trigger the interrupt manually for testing
  // if (millis() % 10000 == 0) // Every 10 seconds
  // {
  //   generateInterrupt();
  // }
}

/**
 * @brief Renders a startup/status screen on the OLED display.
 *
 * Clears the SSD1306 buffer, prints basic device identity text, and shows the
 * current local Wi-Fi IP address before pushing the frame to the display.
 */
void flashSSD()
{
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("ESP32");
  display.println("Client PIO");
  display.println(WiFi.localIP());
  display.display();
}

/**
 * @brief Refreshes the widgets by fetching sensor data, updating the Blynk terminal,
 *        and writing relevant data to virtual pins.
 *
 * This function is called periodically by a timer. It performs the following tasks:
 * - Fetches the list of connected sensors from a remote server using an HTTP GET request.
 * - Parses the fetched sensor data and updates the internal state if there are changes.
 * - Updates the Blynk terminal with the list of connected sensors and their IP addresses.
 * - Writes various statistics (e.g., pass, fail, recovered, retry counts, and last message)
 *   to specific Blynk virtual pins.
 *
 * @note If the HTTP GET request fails or no sensors are connected, the function logs an error
 *       message and exits early.
 * */
void refreshWidgets() // called every x seconds by SimpleTimer
{
  char tmp[256];
  String sensorsConnected = performHttpGet(ipList);
  // Serial.printf("sensor connrcted %s\n", sensorsConnected.c_str());
  if (sensorsConnected.isEmpty())
  {
    sprintf(tmp, "Failed to fetch sensors from mySQL ");
    Blynk.virtualWrite(V39, tmp);
    return;
  }
  if (!getSensorData(sensorsConnected))
  {
    sprintf(tmp, "No sensors connected to network");
    Blynk.virtualWrite(V39, tmp);
    return;
  }

  if (lastSensorsConnected != sensorsConnected) // only update Blynk terminal when IP list changes
  {
    lastSensorsConnected = sensorsConnected;
    Blynk.virtualWrite(V46, "\nStart:\n");
    for (const auto &pair : ipMap)
    {
      String identifier = pair.first.c_str(); // remove the unique name identifier
      identifier = identifier.substring(0, identifier.length() - 2);
      Serial.printf("Sensor: %s, IP: %s\n", identifier.c_str(), pair.second.c_str());
      sprintf(tmp, "\tSensor: %s, IP: %s\n", identifier.c_str(), pair.second.c_str());
      Blynk.virtualWrite(V49, tmp);
    }
    sprintf(tmp, "\n\tenter 'list' for valid commands\n");
    Blynk.virtualWrite(V49, tmp);
  }

  Blynk.virtualWrite(V7, passSocket);
  Blynk.virtualWrite(V20, failSocket);
  Blynk.virtualWrite(V19, recoveredSocket);
  Blynk.virtualWrite(V34, retry);
  Blynk.virtualWrite(V47, lastMsg);
}
/**
 * @brief Callback function that is triggered when the device connects to the Blynk server.
 *
 * This function performs the following tasks:
 * - Resets the counters for failed and recovered socket connections, as well as retry attempts.
 * - Checks the connection status to the Blynk server. If not connected, it restarts the ESP device.
 * - Logs the connection status to the serial monitor.
 * - Sends the last boot time, reset reason, and other diagnostic data to specific virtual pins on the Blynk server.
 * - Performs an HTTP GET request to retrieve data, processes the response, and updates the corresponding virtual pin.
 * - Refreshes widgets and updates the passSocket value based on the HTTP response.
 *
 * @note If the HTTP request fails, the function logs an error message and exits early.
 */
BLYNK_CONNECTED()
{

  bool isconnected = Blynk.connected();
  if (isconnected == false)
  {
    Serial.println("Blynk Not Connected");
    ESP.restart();
  }
  else
    Serial.println("Blynk Connected");

  getBootTime(lastBoot, strReason);
  Blynk.virtualWrite(V25, lastBoot);
  Blynk.virtualWrite(V26, strReason);
  Blynk.virtualWrite(VFAIL, 0);  // reset failed socket
  Blynk.virtualWrite(VRECOV, 0); //   "   recover
  Blynk.virtualWrite(VRETRY, 0); //   "   retry
  Blynk.virtualWrite(V39, "boot");

  String payload = performHttpGet(getRowCnt);
  if (payload.isEmpty())
  {
    Serial.println("Failed to HHTP request ");
    return;
  }
  else
  {
    refreshWidgets();
    passSocket = payload.toInt();
    Blynk.virtualWrite(V7, passSocket);

    Serial.printf("passSocket %d  \n", passSocket);
  }
}
/**
 * @brief Handles a Blynk command to clear the remote IP list of connected devices.
 *
 * Triggered by virtual pin V18. Sends an HTTP GET request to `ipDelete` on the
 * backend server. If the request fails or returns an empty payload, an error is
 * logged to Serial and no further action is taken.
 */
BLYNK_WRITE(V18)
{
  String payload = performHttpGet(ipDelete);
  if (payload.isEmpty())
  {
    Serial.println("Failed to fetch ip for connected devices or no devices connected");
    return;
  }
}
/**
 * @brief Sends a BLK test command to every device currently stored in `ipMap`.
 *
 * Triggered by virtual pin `BLINK_TST`. Temporarily disables the periodic refresh
 * timer to avoid overlap with socket traffic, iterates through all known sensor IPs,
 * and sends the "BLK" command through `socketClient()`. Each response is printed to
 * Serial, then the refresh timer is re-enabled.
 */
BLYNK_WRITE(BLINK_TST)
{
  timer.disable(timerID1);
  char *str = nullptr;
  // Iterate through the map
  for (const auto &pair : ipMap)
  {
    Serial.printf("Key: %s, Value: %s\n", pair.first.c_str(), pair.second.c_str());
    str = socketClient((char *)pair.second.c_str(), (char *)"BLK");
    Serial.printf("blk_tst %s \n", str);
    free(str);
  }
  timer.enable(timerID1);

  // int index = param.asInt();
  //  char *str = socketClient((char *)ipAddr[index], (char *)"TST");
  //  String foo = String(str);
  //  index = foo.indexOf(":");
  //  skip crc
  //  Blynk.virtualWrite(V12, (foo.substring(index + 1)));
  //  free(str);
  //  timer.enable(timerID1);
}
/**
 * @brief ISR (Interrupt Service Routine) for handling the lightweight watchdog timer (LWD).
 *
 * This function is marked with `ICACHE_RAM_ATTR` to ensure it is placed in IRAM,
 * allowing it to be executed during interrupt handling. It checks if the elapsed
 * time since the last watchdog reset exceeds the defined timeout (`LWD_TIMEOUT`)
 * or if there is an inconsistency in the timeout calculation. If either condition
 * is met, it logs the event, writes a status to a Blynk virtual pin, queues the
 * current status, and restarts the ESP device.
 *
 * @note This function is intended to be called as an interrupt callback and
 *       should execute as quickly as possible to avoid interrupt blocking.
 *
 * @warning Restarting the ESP device will cause all current operations to stop
 *          and the device to reboot.
 */
void ICACHE_RAM_ATTR lwdtcb(void)
{
  // Serial.println("Interrupt generated!");

  if ((millis() - lwdTime > LWD_TIMEOUT) || (lwdTimeout - lwdTime != LWD_TIMEOUT))
  {
    // Blynk.logEvent("3rd_WDTimer");
    Serial.printf("3rd_WDTimer esp.restart %lu %lu\n", (millis() - lwdTime), (lwdTimeout - lwdTime));
    Blynk.virtualWrite(V39, "3rd_WDTimer");
    queStat();
    ESP.restart();
  }
}
/**
 * @brief Resets the lightweight watchdog timer by updating the current time and timeout.
 *
 * This function sets the `lwdTime` variable to the current time (in milliseconds)
 * and calculates the new timeout value by adding the predefined `LWD_TIMEOUT`
 * to the current time. It ensures that the lightweight watchdog timer does not
 * trigger a timeout as long as this function is called periodically.
 */
void lwdtFeed(void)
{
  lwdTime = millis();
  lwdTimeout = lwdTime + LWD_TIMEOUT;
}

/**
 * @brief Updates the widget values in the Blynk application based on the sensor data.
 *
 * This function takes the sensor name and an array of floating-point values (tokens)
 * representing sensor readings. It updates the corresponding virtual pins in the Blynk
 * application to display the sensor data.
 *
 * @param sensor A character pointer to the name of the sensor (e.g., "BME280", "BMP390", "SHT35", "ADS1115").
 * @param tokens An array of floating-point values representing the sensor readings.
 *               The specific indices used depend on the sensor type.
 *
 * @note The function supports the following sensors:
 *       - "BME280" or "BMP390": Updates temperature (V4). If "BME280" or "SHT35", also updates humidity (V6).
 *       - "ADS1115": Updates a gauge (GAUGE_HOUSE) and another virtual pin (V43) based on calculations.
 *
 * @note Debugging information can be enabled by defining DEBUG_W, which prints sensor data to the Serial monitor.
 */
void upDateWidget(char *sensor, float tokens[])
{
  // #define DEBUG_W
  String localSensorName = sensor;
#ifdef DEBUG_W
  Serial.printf("sensor %s\n", localSensorName.c_str());
  for (int j = 0; j < 5; j++)
  {
    Serial.printf(" %d ", j);
    Serial.printf(" %f ", tokens[j]);
  }
  Serial.println();
#endif
  if (localSensorName == "BME280" || localSensorName == "BMP390" || localSensorName == "SHT35")
  {
    Blynk.virtualWrite(V4, tokens[1]); // display temp to android app
    if (localSensorName == "BME280" || localSensorName == "SHT35")
      Blynk.virtualWrite(V6, tokens[2]); // display humidity
    return;
  }
  // if (localSensorName == "SHT35")
  // {
  //   Blynk.virtualWrite(V5, tokens[1]);
  //   Blynk.virtualWrite(V15, tokens[2]);
  //   return;
  // }
  if (localSensorName == "ADS1115")
  {
    Blynk.virtualWrite(V2, tokens[1] * tokens[3]); // display Jackery Volt
    Blynk.virtualWrite(V43, tokens[2]);            // display v++ for esp32

    return;
  }
}
/**
 * @brief Performs an HTTP GET request to the specified URL and retrieves the response as a string.
 *
 * @param url The URL to send the HTTP GET request to. Must be a null-terminated C-style string.
 * @return String The response payload as a string if the request is successful.
 *         Returns an empty string if the request fails or the HTTP response code is not 200.
 *
 * @note If the macro DEBUG_PHP is defined, the response payload will be printed to the Serial monitor.
 */
// #define DEBUG_PHP
String performHttpGet(const char *url)
{
  http.begin(url);
  int httpResponseCode = http.GET();
  if (httpResponseCode != 200)
  {
    Serial.printf("HTTP GET failed with code: %d\n", httpResponseCode);
    return ""; // Return an empty string on failure
  }
  String response = http.getString();
  http.end();

#ifdef DEBUG_PHP
  Serial.printf("url: %s Payload: %s\n", url, response.c_str());
#endif
  return response;
}

/**
 * @brief Parses a string containing information about connected sensors and their IP addresses,
 *        and stores the sensor names and IPs in a map. Additionally, attempts to read sensor data
 *        from each connected device using a socket client.
 *
 * @param sensorsConnected A formatted string containing the number of devices and their details.
 *        Format: "<number_of_devices>|<sensor_name1>:<ip1>|<sensor_name2>:<ip2>|..."
 *        Example: "2|DS1_DS1:192.168.1.5|BMP:192.168.1.7|"
 *
 * @note The function assumes that the input string is well-formed and contains valid data.
 *       Debugging information can be enabled by defining the macros DEBUG_LIST and DEBUG.
 *
 * @details The function performs the following steps:
 *          1. Extracts the number of devices from the input string.
 *          2. Iterates through each device's information, extracting the sensor name and IP address.
 *          3. Appends "_<index>" to each sensor name before storing it as an `ipMap` key to prevent
 *             key collisions when multiple boards report the same sensor type (e.g. two DS18B20 nodes).
 *          4. Stores the unique sensor name and IP address in `ipMap`.
 *          5. Attempts to read sensor data from each device using the `socketClient` function.
 *          6. Logs debugging information if the DEBUG or DEBUG_LIST macros are defined.
 *
 * @warning The function modifies the input string `sensorsConnected` during processing.
 *          Ensure that the input string is not needed elsewhere in its original form.
 *
 * @note If the `socketClient` function fails for a device, an error message is printed to the serial monitor.
 */
int getSensorData(const String &sensorsConnected)
{

  String rows = sensorsConnected.substring(0, sensorsConnected.indexOf("|"));
  int numberOfRows = atoi(rows.c_str());

#ifdef DEBUG_LIST
  Serial.printf("list of devices: %s", sensorsConnected.c_str()); // warning "\n" in sensorConnected string
#endif

  String sensorConnected = sensorsConnected.substring(sensorsConnected.indexOf("|") + 1,
                                                      sensorsConnected.lastIndexOf("|"));

  ipMap.clear(); // if sensor was removed (failed to connect) need to clear!!!!!
  for (int i = 0; i < numberOfRows; i++)
  {
    int index = sensorConnected.indexOf(":");
    int index1 = sensorConnected.indexOf(",");
    int index2 = sensorConnected.indexOf("|");
    String ip = sensorConnected.substring(index + 1, index2);
    String sensorName = sensorConnected.substring(index1 + 1, index);

    sensorName = sensorName + "_" + i; // create unique name to fix the bug below!

    // update map with IP address , used downstream for connecting to server from terminal(V48)
    ipMap[sensorName.c_str()] = ip.c_str(); // bug when multpule boards of the same sensor name
// #define DEBUG_1
#ifdef DEBUG_1
    Serial.printf("Sensor: %s, IP: %s\n", sensorName.c_str(), ip.c_str());
#endif

    int rc = socketClient((char *)ip.c_str(), (char *)"ALL", 1); // read sensor data from connected device
    if (rc)
    {
      socketRecovery((char *)ip.c_str(), (char *)"ALL"); // current failed write to error recovery queue
      failSocket++;
    }
    processSensorData(tokens);

    sensorConnected = sensorConnected.substring(index2 + 1); // Move to the next device in string

#ifdef DEBUG
    Serial.printf("device connect %s \n ", sensorConnected.c_str());
#endif
  } // end for
  return numberOfRows;
}
/**
 * @brief Handles input from the Blynk terminal widget.
 *
 * This function is triggered whenever a string is sent to the virtual pin V49
 * (configured as a terminal widget in the Blynk app). It reads the input string
 * and logs it to the serial monitor for further processing.
 *
 * Supported commands:
 * - "list":  prints all valid commands to V49.
 * - "reboot": calls `queStat()` then restarts the ESP32.
 * - "up":    writes formatted uptime (days/hours/minutes/seconds) to V49.
 * - "ping":  TCP-pings every entry in `ipMap` 4 times and HTTP-pings `ipList` 4 times;
 *            reports pass/dead counts and elapsed time to V49.
 * - "adc":   fetches ADS1115 voltage reading for matching nodes via `getSensorData4User()`.
 * - "bme":   fetches BME280 temperature reading for matching nodes via `getSensorData4User()`.
 * - "bmx":   fetches BMP390 temperature reading for matching nodes via `getSensorData4User()`.
 * - "ds1":   fetches DS18B20 temperature reading for matching nodes via `getSensorData4User()`.
 * - "refr":  clears `lastSensorsConnected`, calls `refreshWidgets()`, and resets
 *            fail/recovered/retry counters.
 * Unrecognised input writes an error message to V49.
 *
 * @param param The parameter object containing the string sent to the terminal widget.
 */
BLYNK_WRITE(V49)
{
  String validCommand[] = {"list", "reboot", "ping", "up", "adc", "bme", "bmx", "ds1"};
  char tmp[512];
  int numberOfElements = sizeof(validCommand) / sizeof(validCommand[0]);

  String input = param.asStr(); // Read the input string from the terminal
  if (input.isEmpty())
  {
    Serial.println("Invalid parameter received.");
    return;
  }
  input.toLowerCase();

  Serial.printf("Received from terminal: %s\n", input.c_str());

  if (input.startsWith("list"))
  {
    for (int i = 0; i < numberOfElements; i++)
    {
      Serial.println(validCommand[i]);
      sprintf(tmp, "%s %s", validCommand[i].c_str(), "\n");
      Blynk.virtualWrite(V49, tmp);
    }
  }
  else if (input.startsWith("reboot"))
  {
    Serial.println("Reboot command received. Restarting...");
    queStat();
    ESP.restart();
  }
  else if (input.startsWith("up"))
    printUptime();

  else if (input.startsWith("bmx") || input.startsWith("bme") || input.startsWith("adc") || input.startsWith("ds1"))
  {
    getSensorData4User(input);
  }
  else if (input.startsWith("refr"))
  {
    lastSensorsConnected = ""; // force output
    refreshWidgets();
    failSocket = recoveredSocket = retry = 0;
  }
  else if (input.startsWith("ping"))
  {
    ping();
  }
  // else if (input.startsWith("test"))
  else
  {
    sprintf(tmp, "command %s not vaild \n", input.c_str());
    Blynk.virtualWrite(V49, tmp);
  }
}
void printUptime()
{
  unsigned long uptimeMillis = millis(); // Get uptime in milliseconds

  // Calculate days, hours, minutes, and seconds
  unsigned long seconds = uptimeMillis / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  char tmp[80];
  seconds %= 60;
  minutes %= 60;
  hours %= 24;

  // Print uptime to the serial monitor
  sprintf(tmp, "Uptime: %lu days, %lu hours, %lu minutes, %lu seconds\n", days, hours, minutes, seconds);
  Blynk.virtualWrite(V49, tmp);
  Serial.printf("Uptime: %lu days, %lu hours, %lu minutes, %lu seconds\n", days, hours, minutes, seconds);
}

/**
 * @brief Checks if a server is reachable by attempting to establish a connection.
 *
 * This function creates a temporary WiFi client and tries to connect to the specified
 * server IP address and port. If the connection is successful, it immediately closes
 * the connection and returns true. Otherwise, it returns false.
 *
 * @param serverIP The IP address of the server to connect to (as a C-style string).
 * @param port The port number of the server to connect to.
 * @return true If the server is reachable.
 * @return false If the server is not reachable.
 */
bool isServerConnected(const char *serverIP, uint16_t port)
{
  WiFiClient client;
  if (client.connect(serverIP, port))
  {
    client.stop(); // Close the connection
    return true;   // Server is reachable
  }
  return false; // Server is not reachable
}
/**
 * @brief Simulates an interrupt by manually calling the ISR for testing purposes.
 *
 * This function disables interrupts, invokes the ISR manually, and then re-enables
 * interrupts. It is useful for testing interrupt handling logic without relying on
 * actual hardware interrupts.
 *
 */
void generateInterrupt()
{
  Serial.println("Interrupt generated!");
  noInterrupts(); // Disable interrupts
  lwdtcb();       // Manually call the ISR for testing
  interrupts();   // Re-enable interrupts
}
/**
 * @brief Checks for the presence of an SSD1306 OLED display on the I2C bus.
 *
 * This function initializes the I2C communication, attempts to communicate with the
 * SSD1306 OLED display at the specified address (SSD_ADDR), and verifies if the device
 * is present. If the device is found, it initializes the display and prints a success
 * message to the serial monitor. If the device is not found or initialization fails,
 * an appropriate message is printed to the serial monitor.
 *
 * @return true if the SSD1306 OLED display is found and initialized successfully,
 *         false otherwise.
 */
bool checkSSD()
{
  byte error;
  int nDevices = 0;
  Wire.begin();
  Wire.beginTransmission(SSD_ADDR);
  error = Wire.endTransmission();
  if (error == 0)
  {                                                     /*if I2C device found*/
    if (!display.begin(SSD1306_SWITCHCAPVCC, SSD_ADDR)) // Address 0x3D for 128x64
      Serial.println(F("SSD1306 allocation failed"));
    else
    {
      nDevices = 1;
      Serial.printf("I2C OLED device found for addr = 0x%x\n", SSD_ADDR); /*print this line if I2C device found*/
    }
  }
  else
    Serial.println("no I2C device found "); /*print this line if I2C device found*/

  return nDevices;
}
/**
 * @brief Retrieves the IP address associated with a given sensor name.
 *
 * This function searches through a map of sensor names and their corresponding
 * IP addresses, performing a case-insensitive comparison to find a match. If a match
 * is found, the associated IP address is returned. If no match is found, the function
 * returns empty string "".
 *
 * @param sensorName The name of the sensor to look up.
 * @return String All matching IP addresses as a `|`-delimited string with a trailing `|`
 *         (e.g. `"192.168.1.5|"`), or an empty string if no match is found.
 */
// #define DEBUG_
String getIP(String sensorName)
{
  String sensorKey = sensorName, returnIPstring = "", mapKey;
  sensorKey.toUpperCase();
  for (const auto &pair : ipMap)
  {
#ifdef DEBUG_
    char tmp[100];
    sprintf(tmp, "Sensor %s ip %s\n", pair.first.c_str(), pair.second.c_str());
    Blynk.virtualWrite(V49, tmp);
#endif
    mapKey = pair.first.c_str();
    mapKey.toUpperCase();
    if (mapKey.indexOf(sensorKey) >= 0)
    {
      returnIPstring.concat(pair.second.c_str());
      returnIPstring.concat("|");
    }
  }
  return returnIPstring;
}
/**
 * @brief Fetches and displays sensor data for a user-requested sensor type via the Blynk terminal (V49).
 *
 * Looks up all IP addresses associated with the given sensor prefix (first 3 characters of @p input,
 * e.g. "bme", "adc", "ds1") using `getIP()`. For each IP, issues a socket "ALL" command to retrieve
 * the current readings, then matches the response tokens against a built-in tag map that maps sensor
 * prefixes to numeric device IDs. Matching readings are formatted and written to virtual pin V49.
 *
 * @param input The user command string whose first 3 characters identify the sensor type.
 *              Supported prefixes: "bmx", "bme", "bmp", "sht", "adc", "ds1".
 *
 * @note For "adc" sensors the raw voltage is multiplied by the voltage-divider ratio stored
 *       in `tokens[i][3]` to produce the actual 12 V reading.
 * @note If no IP is found for the sensor prefix, an error message is written to V49.
 * @note `std::map::find()` is intentionally avoided: constructing a `std::string` key from
 *       `String::c_str()` triggers a lookup bug. The tag map is iterated manually instead.
 */
void getSensorData4User(String input)
{
  const std::map<std::string, int> tagMap =
      {
          {"bmx", 77},
          {"bme", 76},
          {"bmp", 58},
          {"sht", 44},
          {"adc", 48},
          {"ds1", 28}};

  char tmp[512];
  String label = "Temp", postFix = "F";
  if (input.startsWith("adc"))
  {
    label = "Volt";
    postFix = "V";
  }
  String ip = getIP(input.substring(0, 3).c_str());
  if (ip.isEmpty())
  {
    sprintf(tmp, "no ip@ for sensor %s \n", input.c_str());
    Blynk.virtualWrite(V49, tmp);
  }
  else
  {
    int rc = 0;
    while (1)
    {
      int index = ip.indexOf("|");
      if (index >= 0)
      {
        String parseIP = ip.substring(0, index);
        rc = socketClient((char *)parseIP.c_str(), (char *)"ALL", 0);
        if (rc)
          Serial.println("socketClient() failed");
        else
        {
          // .find() has bugs when passing input.c_str() but works if hard code "adc"
          // auto it = tagMap.find((std::string)input.c_str());
          // auto it = tagMap.find("adc");
          // if (it != tagMap.end())
          // {
          //   chipID = it->second; // Access value
          // }
          // Serial.printf("senur chip ID %d from blynk %s\n", chipID, input.c_str());
          for (const auto &pair : tagMap)
          {
            String sensor = pair.first.c_str();
            int device = pair.second;
            if (input.substring(0, 3) == sensor)
            {
              // loop all device(s) data
              for (int i = 0; i < 5; i++)
              {
                if (device == tokens[i][0])
                {
                  float ftmp = tokens[i][1];
                  if (input.startsWith("adc"))
                    // value is scaled by divider ratio (`tokens[i][3]`) before display
                    // measuring 12v need voltage div [0][3] contains divider ratio
                    ftmp *= tokens[i][3];

                  sprintf(tmp, "%s %f %s %s \n", label.c_str(), ftmp, postFix.c_str(), parseIP.c_str());
                  Blynk.virtualWrite(V49, tmp);
                }
              }
            }
          }
        }
        ip = ip.substring(index + 1);
      }
      else
        break;
    }
  }
}

void ping()
{
  int dead, alive;
  unsigned long start = millis();
  char tmp[100], tmp1[100];

  // Iterate over all registered sensor-IP pairs and perform a connectivity check.
  // Each device is pinged 4 times via TCP connect/disconnect (isServerConnected).
  // Results (pass/dead counts and elapsed time) are reported back to the Blynk
  // terminal (V49). Setting terminal color on failure was removed: Blynk.setProperty
  // inside a non-BLYNK_WRITE context had no effect (known platform limitation).
  // The `_i` index suffix appended to ipMap keys by getSensorData() is stripped before
  // display so the user-facing sensor name is clean (e.g. "BME280" not "BME280_0").
  for (const auto &pair : ipMap)
  {
    alive = dead = 0;
    String identifier = pair.first.c_str(); // bug fixed!
    identifier = identifier.substring(0, identifier.length() - 2);
    sprintf(tmp, "%s %s:\n", identifier.c_str(), pair.second.c_str());
    for (int j = 0; j < 4; j++)
    {
      if (isServerConnected(pair.second.c_str()))
        alive++;
      else
        dead++;
    }

    sprintf(tmp1, "\tpass %d dead %d  time: %lu ms\n", alive, dead, millis() - start);
    strcat(tmp, tmp1);
    Blynk.virtualWrite(V49, tmp);
  }
  // ping http
  sprintf(tmp, "%s\n", ipList);

  alive = dead = 0;
  start = millis();
  for (int j = 0; j < 4; j++)
  {
    String sensorsConnected = performHttpGet(ipList);
    if (sensorsConnected.isEmpty())
      dead++;
    else
      alive++;
  }
  sprintf(tmp1, "\t%d pass %d dead time: %lu ms\n", alive, dead, millis() - start);
  strcat(tmp, tmp1);
  Blynk.virtualWrite(V49, tmp);
}
//