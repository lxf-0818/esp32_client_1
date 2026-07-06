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
 * - ping(): TCP-pings all netMap entries and HTTP-pings ipList; reports results to V49.
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
#include <tuple>
#include <iostream>
#define INPUT_BUFFER_LIMIT 2048
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define BLYNK_PRINT Serial
#define DEVICES 8

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#define SSD_ADDR 0x3c
void editLoc();
void initRTOS();
void flashSSD();
bool checkSSD();
void blynkWrite(String cmd, int index);
void refreshWidgets();
void resetStats();
void getBootTime(char *lastBook, char *strReason);
int getSensorData(const String &sensorsConnected);
int getSensorData_new();
int createMap();
void getSensorData4User(String input, String ip);
int socketRecovery(char *IP, char *cmd2Send, char *MAC);
void processSensorData(float tokens[DEVICES][5], String sesnor);
String performHttpGet(const char *url);
int decryptWifiCredentials(char *auth, char *ssid, char *psw);
int socketClient(char *espServer, char *command);
char *socketClient(char *espServer, String command);
void upDateWidget(char *sensorName, float tokens[]);
void dumpI2C();
void lwdtFeed(void);
void ICACHE_RAM_ATTR lwdtcb(void);
bool queStat();
bool isServerConnected(const char *serverIP, uint16_t port = 8888);
void generateInterrupt();
void printUptime();
String getIP(String sensorName);
void printTokens(float tokens[DEVICES][5]);
void ping();
void dumpIP();
String mac2room(String sensor);
int parseInput(String input, String validCommand[], int count);
void displayValidCmdList(String validCommand[], int count);
void setupHTTP_request(String sensorName, String location, float tokens[]);
void updateBlynk();
String ip2mac(String ip);
void enableTimer();
void disableTimer();

/**
 * @brief Network metadata for one sensor entry.
 *
 * Stores the node IP address, MAC address, and human-readable location.
 */
typedef struct
{
  std::string ipAddress;
  std::string macAddress;
  std::string location;
} net_t;
net_t netword;

// Indexed sensor map: key format is "<SENSOR>_<index>" (for example "BME_0").
std::map<std::string, net_t> netMap;

// Indexed sensor map: key format is <IPv4> (for example "192.168,1,3").
std::map<std::string, net_t> ipMap;

String menuList[] = {"Main Room", "ADC Guest Room", "Mud Room", "Master Bedroom",
                     "Guest Room", "Laundry Room", "BowFlex",
                     "ALL"};
const uint16_t port = 8888;
String sensorName = "NO DEVICE";
int failSocket, passSocket, recoveredSocket, retry, timerID1, passPost;
String sensorsConnected;
HTTPClient http;
String lastMsg;
char lastBoot[20], strReason[60];
BlynkTimer timer;
float tokens[DEVICES][5];
bool setAlarm = false;
Ticker lwdTicker;
String lastSensorsConnected = "";
String phpServerIP;
// bool stop = false;
#define LWD_TIMEOUT 15 * 1000 // Reboot if loop watchdog timer reaches this time out value
unsigned long lwdTime = 0;
unsigned long lwdTimeout = LWD_TIMEOUT;
const char *getRowCnt = "rows.php";
const char *deleteAll = "truncate.php";
const char *ipList = "ip.php";
// #define TEST
#ifdef TEST
const char *ipMacList = "macipTest.php";
#else
const char *ipMacList = "macip.php";
#endif

const char *ipDelete = "deleteMAC.php";

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

  // Serial.println("Turned off timer in setup()");
  timerID1 = timer.setInterval(1000L * 20, refreshWidgets); //
  lwdtFeed();
  lwdTicker.attach_ms(LWD_TIMEOUT, lwdtcb); // attach lwdt callback routine to Ticker object
  initRTOS();
  //refreshWidgets();
  int cnt = createMap();
  Serial.printf(" Sensors: %d\n", cnt);

  Blynk.setProperty(BLINK_TST, "labels",
                    "Main Room", "ADC Guest Room", "Mud Room", "Master Bedroom",
                    "Guest Room", "Laundry Room", "Gym",
                    "ALL");

  Blynk.setProperty(BOOT, "labels",
                    "Main Room", "ADC Guest Room", "Mud Room", "Master Bedroom",
                    "Guest Room", "Laundry Room", "Gym",
                    "ALL");

  // editLoc();
  // Method B: Using emplace for optimal performance
  // netMap.emplace(102, Network{"bob@email.com", 290});
  // labelText = labelText + "\" " + pair.second.location.c_str() + "\", ";
  // labelText = labelText + pair.second.location.c_str() + ",";/int length = (labelText.length() - 1);
  // labelText = labelText.substring(0, length);
  // Serial.println(labelText);
  // Blynk.setProperty(BLINK_TST, "labels", labelText.c_str());
}
/**
 * @brief Main runtime loop for the ESP32 client.
 *
 * Continuously feeds the loop watchdog timer, processes Blynk events, and runs
 * sscheduled timer callbacks (including periodic widget refresh tasks).
 */
void loop()
{
  lwdtFeed();
  Blynk.run();
  timer.run();
#define TEST_LWD_
#ifdef TEST_LWD
  while (1)
  {
    delay(2);
  };
#endif
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
  String location;
  char tmp[256];
  // esp32 still blinking and can ping but blynk app is offline hopefully this works?
  bool isconnected = Blynk.connected();
  if (isconnected == false)
  {
    Serial.println("Blynk Not Connected");
    ESP.restart();
  }
  String sensorsConnected = performHttpGet(ipMacList);
  // Serial.printf("sensorsConnected %s\n", sensorsConnected.c_str());
  if (sensorsConnected.isEmpty())
  {
    sprintf(tmp, "Failed to fetch sensors from mySQL ");
    Blynk.virtualWrite(V39, tmp);
    return;
  }

  int sensorCnt = getSensorData(sensorsConnected);
  if (!sensorCnt)
  {
    sprintf(tmp, "No sensors connected to network\n");
    Blynk.virtualWrite(V39, tmp);
    return;
  }
  Blynk.virtualWrite(V51, sensorCnt);
  Blynk.virtualWrite(V7, passSocket);
  Blynk.virtualWrite(V20, failSocket);
  Blynk.virtualWrite(V19, recoveredSocket);
  Blynk.virtualWrite(V34, retry);
  Blynk.virtualWrite(V47, lastMsg);

  if (lastSensorsConnected != sensorsConnected) // only update Blynk terminal when IP list changes
  {
    lastSensorsConnected = sensorsConnected;
    Blynk.virtualWrite(V46, "\nStart:\n");
    for (const auto &pair : netMap)
    {
      location = pair.second.location.c_str();
      // netMap keys are stored as "<SENSOR>_<n>"; strip "_<n>" before display.
      String sensor = pair.first.c_str();
      sensor = sensor.substring(0, sensor.length() - 2);
      sprintf(tmp, "Sensor: %s  %s \n", sensor.c_str(), location.c_str());
      Blynk.virtualWrite(V49, tmp);
      sprintf(tmp, "\t\tIP: %s \n", pair.second.ipAddress.c_str());
      Blynk.virtualWrite(V49, tmp);
    }
    sprintf(tmp, "\n\tenter 'list' for valid commands\n");
    Blynk.virtualWrite(V49, tmp);
  }
}
void resetStats()
{
  failSocket = recoveredSocket = retry = 0;
  Blynk.virtualWrite(V20, failSocket);
  Blynk.virtualWrite(V19, recoveredSocket);
  Blynk.virtualWrite(V34, retry);
  Blynk.virtualWrite(V47, "");
  Blynk.virtualWrite(V49, "reset complete\n");
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
  String payload;
// #define TEST
#ifdef TEST
  payload = performHttpGet(deleteAll);
  Serial.println("WARNING TRUNCATE DB");
#endif
  payload = performHttpGet(getRowCnt);
  if (payload.isEmpty())
  {
    Serial.println("Failed php script ");
    return;
  }
  else
  {
    passSocket = payload.toInt();
    Blynk.virtualWrite(V7, passSocket);
    Serial.printf("passSocket %d  \n", passSocket);
    failSocket = recoveredSocket = retry = 0;
  }
}
/**
 * @brief Handles a Blynk command to clear backend sensor rows.
 *
 * Triggered by virtual pin V18. Sends an HTTP GET request to `deleteAll`
 * (`/deleteALL.php`) on the backend server. If the request fails or returns an
 * empty payload, an error is logged to Serial and no further action is taken.
 */
BLYNK_WRITE(V18)
{
  String payload = performHttpGet(deleteAll);
  if (payload.isEmpty())
  {
    Serial.println("Failed to fetch ip for connected devices or no devices connected");
    return;
  }
}
/**
 * @brief Sends a BLK test command to every device currently stored in `netMap`.
 *
 * Triggered by virtual pin `BLINK_TST`. Temporarily disables the periodic refresh
 * timer to avoid overlap with socket traffic, iterates through all known sensor IPs,
 * and sends the "BLK" command through `socketClient()`. Each response is printed to
 * Serial, then the refresh timer is re-enabled.
 */
BLYNK_WRITE(BLINK_TST)
{
  timer.disable(timerID1);   // pause periodic refresh to prevent socket contention during test
  int index = param.asInt(); // widget sends the selected button index (0-based)
  blynkWrite("BLK", index);
  timer.enable(timerID1); // restore periodic widget refresh
}
BLYNK_WRITE(BOOT)
{
  timer.disable(timerID1);   // pause periodic refresh to prevent socket contention during test
  int index = param.asInt(); // widget sends the selected button index (0-based)
  blynkWrite("RST", index);
  timer.enable(timerID1); // restore periodic widget refresh
}
/**
 * @brief Loop watchdog callback used by the Ticker timer.
 *
 * This function checks whether loop heartbeat timing drifted beyond
 * `LWD_TIMEOUT` or if the timeout bookkeeping became inconsistent. On timeout,
 * it logs to Serial, writes a status to Blynk, waits briefly for queue drain
 * via `queStat()`, then restarts the ESP32.
 *
 * @note The callback is attached from `setup()` using `lwdTicker.attach_ms(...)`.
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
    // 3rd_WDTimer esp.restart 0 15000
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
 *       - "BME280" or "BMP390" or "SHT35": Updates temperature (V4, tokens[1]).
 *         "BME280" and "SHT35" additionally update humidity (V6, tokens[2]).
 *       - "ADS1115": Updates Jackery voltage gauge (V2/GAUGE_HOUSE, tokens[1] × tokens[3])
 *         and ESP32 supply voltage (V43, tokens[2]).
 *       - "BMP280" and "DS18B20" are not handled; the function returns silently.
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
    // Blynk.virtualWrite(V2, tokens[1] * tokens[3]); // display Jackery Volt
    Blynk.virtualWrite(V2, tokens[1]);  // display Jackery Volt
    Blynk.virtualWrite(V43, tokens[2]); // display v++ for esp32

    return;
  }
}

/**
 * @brief Performs an HTTP GET request to the backend PHP server.
 *
 * Constructs the full URL by prepending the global `phpServerIP` to the
 * provided script path, executes a GET request, and returns the response body.
 * Returns an empty string if the HTTP response code is not 200.
 *
 * @param phpScript Relative path of the PHP script to call (e.g. "/getRow.php").
 * @return String Response payload on success, or an empty string on failure.
 * 
 * @note If the macro DEBUG_PHP is defined, the response payload will be printed to the Serial monitor.
 * 
 */
String performHttpGet(const char *phpScript)
{
  String payload = "";
  String url = phpServerIP + phpScript;
  http.begin(url);
  int httpResponseCode = http.GET();
  if (httpResponseCode != 200)
  {
    Serial.printf("HTTP GET failed %s with code: %d\n", url.c_str(), httpResponseCode);
    payload = ""; // Return an empty string on failure
  }
  else
    payload = http.getString();
#ifdef DEBUG_PHP
  Serial.printf("url: %s Payload: %s\n", url, response.c_str());
#endif
  http.end();
  return payload;
}

/**
 * @brief Parses the backend roster payload and refreshes live data from all nodes.
 *
 * Payload contract expected from ip.php:
 * "<rows>|<SENSOR_OR_GROUP>:<IP>,<LOCATION>,<MAC>|...|"
 *
 * Example:
 * " 2|BME:192.168.1.10,Mud Room-58:BF:25:DA:AE:59|BMX_BME:192.168.1.13,Main Room-48:55:19:ED:B8:B4|"
 *
 * Parsing behavior:
 * - `<rows>` controls loop count (number of tuples expected in the payload body).
 * - `<SENSOR_OR_GROUP>` may contain multiple tags joined by `_` (for example `BME_BMP`).
 * - Grouped tags are split into individual map keys in the form `<SENSOR>_<index>`.
 *
 * Refresh behavior per tuple:
 * - Rebuilds `netMap` (sensor key -> IP) and `locMap` (IP -> location) from scratch.
 * - Polls each parsed IP using socket command `ALL`.
 * - On socket failure, queues the request in recovery queue and increments `failSocket`.
 * - else forwards parsed token data to `processSensorData()`.
 *
 * @param sensorsConnected Delimited roster payload returned by ip.php.
 * @return int Row count parsed from the payload header.
 */

int getSensorData(const String &sensorsConnected)
{
  int z = 0, cnt = 0;
  String name;

  // Header before first '|' is row count sent by the backend.
  String rows = sensorsConnected.substring(0, sensorsConnected.indexOf("|"));
  int numberOfRows = atoi(rows.c_str());
  // Serial.printf("sensors connected %s\n", sensorsConnected.c_str());
  // Slice off the payload body: "sensor:ip,location|sensor:ip,location|..."
  String sensorConnected = sensorsConnected.substring(sensorsConnected.indexOf("|") + 1,
                                                      sensorsConnected.lastIndexOf("|"));
  // Rebuild maps each refresh so stale/disconnected devices are removed.
  netMap.clear();

  for (int i = 0; i < numberOfRows; i++)
  {
    // Example tuple stream:
    // BME:192.168.1.10,Mud Room-58:BF:25:DA:AE:59|BMX_BME:192.168.1.13,Main Room-48:55:19:ED:B8:B4|
    // Parse one device tuple: "sensor_or_group:ip,location|".
    int index = sensorConnected.indexOf(":");
    String sensorName = sensorConnected.substring(0, index) + "_"; // add end of string token

    int index1 = sensorConnected.indexOf(",");
    String ip = sensorConnected.substring(index + 1, index1);

    int index2 = sensorConnected.indexOf("-");
    String location = sensorConnected.substring(index1 + 1, index2);

    int index3 = sensorConnected.indexOf("|");
    String mac = sensorConnected.substring(index2 + 1, index3);

    // Point to the next char in the "device string" for next pass
    sensorConnected = sensorConnected.substring(index3 + 1);

    // Expand grouped names like "BME_BMP" or single "BME" into unique keys: BME_<z>, BMP_<z+1>.
    while (1)
    {
      int j = sensorName.indexOf("_");
      // Serial.printf("sensor %s len %d index %d \n", sensorName.c_str(), sensorName.length(),j);
      if (j > 0)
      {
        name = sensorName.substring(0, j);
        name = name + "_" + z++; // make unique key
        netMap[name.c_str()] = {ip.c_str(), mac.c_str(), location.c_str()};
        sensorName = sensorName.substring(j + 1);
        cnt++;
      }
      else
        break;
    }
    memset(tokens, 0, sizeof(tokens));
    int rc = socketClient((char *)ip.c_str(), (char *)"ALL"); // read sensor data from connected device
    if (rc)
    {
      String error = ">>> socketClient failed ";
      switch (rc)
      {
      case 1:
        error += "to connect";
        break;
      case 2:
        error += "timeout";
        break;
      case 3:
        error += "CRC invalid";
        break;
      }
      lastMsg = "socketClient failed:" + ip;
      // On socket failure, queue recovery and account the failed poll.
      Serial.printf("%s %s %s rc: %d\n", error.c_str(), ip.c_str(), location.c_str(), rc);
      socketRecovery((char *)ip.c_str(), (char *)"ALL", (char *)name.c_str()); // current failed write to error recovery queue
      failSocket++;
    }
    else
    {
      processSensorData(tokens, name.c_str());
    }

  } // end for

  return cnt;
}

/**
 * @brief Refreshes live readings by polling each unique node from ipMap.
 *
 * Flow:
 * - Calls createMap() to rebuild netMap/ipMap from the backend roster.
 * - Iterates ipMap (one entry per device IP) and sends socket command ALL.
 * - On success, forwards parsed tokens to processSensorData().
 * - On failure, logs reason, enqueues recovery, and increments failSocket.
 *
 * @return int Count returned by createMap() (sensor-key count).
 */
int getSensorData_new()
{
  // Rebuild maps first so this polling pass uses the latest backend roster.
  int cnt = createMap();

  // Poll each unique IP once; ipMap is de-duplicated by device IP.
  for (const auto &pair : ipMap)
  {
    // Keep context strings for logs and recovery queue payload.
    String location = pair.second.location.c_str();
    String name = pair.first.c_str();
    String ip = pair.second.ipAddress.c_str();

    // Clear shared token buffer before each node poll.
    memset(tokens, 0, sizeof(tokens));
    int rc = socketClient((char *)ip.c_str(), (char *)"ALL"); // read sensor data from connected device
    if (rc)
    {
      String error = ">>> socketClient failed ";
      switch (rc)
      {
      case 1:
        error += "to connect";
        break;
      case 2:
        error += "timeout";
        break;
      case 3:
        error += "CRC invalid";
        break;
      }
      lastMsg = "socketClient failed:" + ip;
      // On socket failure, queue recovery and account the failed poll.
      Serial.printf("%s %s %s rc: %d\n", error.c_str(), ip.c_str(), location.c_str(), rc);
      socketRecovery((char *)ip.c_str(), (char *)"ALL", (char *)name.c_str()); // current failed write to error recovery queue
      failSocket++;
    }
    else
    {
      processSensorData(tokens, name.c_str());
    }
  }
  return cnt;
}

/**
 * @brief Builds in-memory sensor maps from the backend MAC/IP roster payload.
 *
 * Expected payload format from ipMacList endpoint:
 * rows|SENSOR_OR_GROUP:IP,LOCATION-MAC|...|
 *
 * Behavior:
 * - Fetches the roster from PHP backend.
 * - Rebuilds netMap from scratch using unique keys (SENSOR_index).
 * - Rebuilds ipMap as a de-duplicated view keyed by IP address.
 *
 * @return int Number of sensor keys inserted into netMap.
 *         Returns 1 when backend fetch fails (legacy behavior).
 */
int createMap()
{
  char tmp[256];
  int z = 0, cnt = 0;
  String name;
  String sensorsConnected = performHttpGet(ipMacList);
  // Serial.printf("sensorsConnected %s\n", sensorsConnected.c_str());
  if (sensorsConnected.isEmpty())
  {
    sprintf(tmp, "Failed to fetch sensors from mySQL ");
    Blynk.virtualWrite(V39, tmp);
    return 1;
  }

  // Header before first '|' is row count sent by the backend.
  String rows = sensorsConnected.substring(0, sensorsConnected.indexOf("|"));
  int numberOfRows = atoi(rows.c_str());
  // Serial.printf("sensors connected %s\n", sensorsConnected.c_str());
  // Slice off the payload body: "sensor:ip,location-mac|sensor:ip,location-mac|..."
  String sensorConnected = sensorsConnected.substring(sensorsConnected.indexOf("|") + 1,
                                                      sensorsConnected.lastIndexOf("|"));
  // Rebuild maps each refresh so stale/disconnected devices are removed.
  netMap.clear();

  for (int i = 0; i < numberOfRows; i++)
  {
    // Example tuple stream:
    // BME:192.168.1.10,Mud Room-58:BF:25:DA:AE:59|BMX_BME:192.168.1.13,Main Room-48:55:19:ED:B8:B4|
    // Parse one device tuple: "sensor_or_group:ip,location|".
    // Field delimiters within one tuple: sensor:ip,location-mac|
    int index = sensorConnected.indexOf(":");
    String sensorName = sensorConnected.substring(0, index) + "_"; // add end of string token

    int index1 = sensorConnected.indexOf(",");
    String ip = sensorConnected.substring(index + 1, index1);

    int index2 = sensorConnected.indexOf("-");
    String location = sensorConnected.substring(index1 + 1, index2);

    int index3 = sensorConnected.indexOf("|");
    String mac = sensorConnected.substring(index2 + 1, index3);

    // Point to the next char in the "device string" for next pass
    sensorConnected = sensorConnected.substring(index3 + 1);

    // Expand grouped names like "BME_BMP" or single "BME" into unique keys: BME_<z>, BMP_<z+1>.
    while (1)
    {
      int j = sensorName.indexOf("_");
      // Serial.printf("sensor %s len %d index %d \n", sensorName.c_str(), sensorName.length(),j);
      if (j > 0)
      {
        name = sensorName.substring(0, j);
        name = name + "_" + z++; // make unique key
        netMap[name.c_str()] = {ip.c_str(), mac.c_str(), location.c_str()};
        sensorName = sensorName.substring(j + 1);
        cnt++;
      }
      else
        break;
    }
  } // end for
  // Build IP-indexed map to collapse multi-sensor devices into one reachable node entry.
  for (const auto &pair : netMap)
  {
    // Serial.printf("create map %s\n", pair.first.c_str());
    ipMap[pair.second.ipAddress.c_str()] = {pair.second.ipAddress.c_str(),
                                            pair.second.macAddress.c_str(),
                                            pair.second.location.c_str()};
  }

  return cnt;
}

/**
 * @brief Prints the terminal command list to Blynk virtual pin V49.
 *
 * Iterates through the provided command array and writes one command per line
 * to the Blynk terminal widget. Output format is `<command>\n`.
 *
 * @param validCommand Array of command strings to print.
 * @param numberOfElements Number of valid entries in `validCommand`.
 */
void displayValidCmdList(String validCommand[], int numberOfElements)
{
  char tmp[20];
  for (int i = 0; i < numberOfElements; i++)
  {
    // Serial.println(validCommand[i]);
    sprintf(tmp, "%s\n", validCommand[i].c_str());
    Blynk.virtualWrite(V49, tmp);
  }
}

/**
 * @brief Parses terminal input and resolves it to a command index.
 *
 * Normalizes input to lowercase, then scans `validCommand[]` in order and
 * returns the first entry that matches via `startsWith()`.
 *
 * Return codes:
 * - `>= 0`: matched command index in `validCommand[]`.
 * - `-1`: empty input.
 * - `-2`: no command match; also writes an error to terminal pin V49.
 *
 * @param input User-entered command string from the Blynk terminal.
 * @param validCommand Array of valid command tokens.
 * @param numberOfElements Number of valid entries in `validCommand`.
 * @return int Command index on success, or a negative error code on failure.
 */
int parseInput(String input, String validCommand[], int numberOfElements)
{
  char tmp[512];
  int indexSelected;
  bool found = false;
  if (input.isEmpty())
  {
    Serial.println("Invalid parameter received.");
    return -1;
  }
  input.toLowerCase();

  for (indexSelected = 0; indexSelected < numberOfElements; indexSelected++)
  {
    if (input.startsWith(validCommand[indexSelected]))
    {
      found = true;
      break;
    }
  }
  if (!found)
  {
    sprintf(tmp, "command _%s_ not vaild \n", input.c_str());
    Blynk.virtualWrite(V49, tmp);
    return -2;
  }
  return indexSelected;
}

/**
 * @brief Handles input from the Blynk terminal widget.
 *
 * This function is triggered whenever a string is sent to the virtual pin V49
 * (configured as a terminal widget in the Blynk app). It reads the input string
 * and dispatches a matching command handler.
 *
 * Supported commands:
 * - "list": prints all valid terminal commands to V49.
 * - "reboot": calls `queStat()`, writes completion status if queues are drained,
 *   then restarts the ESP32.
 * - "ping": TCP-pings every entry in `ipMap` and HTTP-pings `ipList`, then reports
 *   pass/dead counts and elapsed time to V49.
 * - "up": writes formatted uptime (days/hours/minutes/seconds) to V49.
 * - "reset": resets fail/recovered/retry counters and related status output.
 * - "refr": clears `lastSensorsConnected` then forces `refreshWidgets()`.
 * - "i2c": polls each known node and prints I2C pin/address mappings.
 * - "ip": prints the de-duplicated IP/location list from `ipMap`.
 * - "all": temporarily pauses periodic refresh, polls each known node for live
 *   readings, then restores the refresh timer.
 *
 * Command matching uses `startsWith`; the first matching prefix in
 * `validCommand[]` is selected.
 *
 * Invalid or empty input returns early after parse validation. Unrecognized
 * commands write an error message to V49 in `parseInput()`.
 *
 * @param param The parameter object containing the string sent to the terminal widget.
 */
BLYNK_WRITE(V49)
{
  String validCommand[] = {
      "list",
      "reboot",
      "ping",
      "up",
      "reset",
      "refr",
      "i2c",
      "ip",
      "enable",
      "disable",
      "all",
  };

  String input = param.asStr(); // Read the input string from the terminal
  int numberOfElements = sizeof(validCommand) / sizeof(validCommand[0]);
  int indexSelected = parseInput(input, validCommand, numberOfElements);
  // Serial.printf("index=%d\n", indexSelected);
  if (indexSelected < 0)
    return;

  switch (indexSelected)
  {
  case 0:
    displayValidCmdList(validCommand, numberOfElements);
    break;
  case 1:
    if (queStat())
      Blynk.virtualWrite(V49, "all tasks complete\n");
    ESP.restart();
    break;
  case 2:
    ping();
    break;
  case 3:
    printUptime();
    break;
  case 4:
    resetStats();
    break;
  case 5:
    lastSensorsConnected = "";
    refreshWidgets();
    break;
  case 6:
    dumpI2C();
    break;
  case 7:
    dumpIP();
    break;
  case 8:
    enableTimer();
    break;
  case 9:
    disableTimer();
    break;
  case 10:
    timer.disable(timerID1); // pause periodic refresh to prevent socket contention during user input
    for (const auto &pair : netMap)
      getSensorData4User(pair.first.c_str(), pair.second.ipAddress.c_str());
    timer.enable(timerID1);
    break;
  }
}
/**
 * @brief Prints a de-duplicated IP/location list to the Blynk terminal.
 *
 * Builds a temporary map keyed by `ipAddress` from `netMap` so devices that expose
 * multiple sensor keys are shown once. For each unique IP, writes one formatted line
 * to virtual terminal V49.
 *
 * Output format per line:
 * - `<ipAddress>\t <location>`
 */
void dumpIP()
{
  char tmp[100];
  for (const auto &pair : ipMap)
  {
    if (pair.second.location == "unknown")
    {
      sprintf(tmp, "loc %s Main Room", pair.second.macAddress.c_str());
      Blynk.virtualWrite(V52, tmp);
    }
    else
    {
      sprintf(tmp, "%12s\t %s\n", pair.second.ipAddress.c_str(), pair.second.location.c_str());
      Blynk.virtualWrite(V49, tmp);
    }
  }
}

/**
 * @brief Writes formatted uptime to the Blynk terminal widget (V49).
 *
 * Converts `millis()` into days/hours/minutes/seconds and publishes a single
 * formatted line to the terminal.
 */
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
  sprintf(tmp, "Uptime: %lu days, %lu hours, %lu minutes, %lu seconds\n",
          days, hours, minutes, seconds);
  Blynk.virtualWrite(V49, tmp);
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
 * IP addresses, performing a comparison to find a match. If a match
 * is found, the associated IP address is returned. If no match is found, the function
 * returns empty string "".
 *
 * @note This function is used only when interfacing with the blynk terminal app
 *
 * @param sensorName The name of the sensor to look up.
 * @return String All matching IP addresses as a `|`-delimited string with a trailing `|`
 *         (e.g. `"192.168.1.5|"`), or an empty string if no match is found.
 */
// #define DEBUG_
// String getIP(String sensorName)
// {
//   String sensorKey = sensorName, returnIPstring = "", mapKey;
//   sensorKey.toUpperCase();
//   for (const auto &pair : netMap)
//   {
//     mapKey = pair.first.c_str();
//     if (mapKey.indexOf(sensorKey) >= 0)

//     {
//       returnIPstring.concat(pair.second.c_str());
//       returnIPstring.concat("|");
//     }
//   }
//   return returnIPstring;
// }

void getSensorData4User(String userInput, String ip)
{
  // Map 3-letter sensor prefixes to their device ID codes (used in tokens[i][0]).
  static const std::map<String, int> tagMap =
      {
          {"bm6", 78},  // BME680
          {"bmx", 77},  // BMP390
          {"bme", 76},  // BME280
          {"bmp", 58},  // BMP280
          {"sht", 44},  // SHT35
          {"adc", 48},  // ADS1115
          {"ds1", 28}}; // DS18B20

  char tmp[512];
  String sensor = userInput;
  userInput = userInput.substring(0, 3);
  userInput.toLowerCase();

  // Set output label and unit: default is temperature in Fahrenheit.
  String label = "Temp", postFix = "F", postFix2 = "%";
  if (userInput.startsWith("adc"))
  {
    // ADS1115 (analog-to-digital converter) outputs voltage
    label = "Volt";
    postFix = "V";
    postFix2 = "V";
  }
  if (userInput.startsWith("bmp") || userInput.startsWith("bmx"))
    postFix2 = "Pa";

  // Poll the target IP for sensor data using the "ALL" command.
  // Return code 0 = success; non-zero = socket error.
  memset(tokens, 0, sizeof(tokens));
  if (socketClient((char *)ip.c_str(), (char *)"ALL"))
  {
    Serial.println("socketClient() failed");
    return;
  }
  // Look up the user-provided sensor prefix in the tag map.
  auto it = tagMap.find(userInput);
  if (it == tagMap.end())
  {
    Serial.printf("device not found .%s.\n", userInput.c_str());
    return; // Not found: abort.
  }
  int device = it->second; // Use the device code from the tag map.

  // Scan the tokens buffer (populated by socketClient) for matching device entries.
  // tokens[i][0] contains the device code; tokens[i][1] contains the reading value.
  bool deviceFound = false;
  for (int i = 0; i < 5; i++)
  {
    if (device == tokens[i][0])
    {
      deviceFound = true;
      // Resolve the target MAC to a human-readable room/location label.
      String room = mac2room(sensor.c_str());
      sprintf(tmp, "%s %.1f %s %.1f %s %s \n",
              label.c_str(), tokens[i][1], postFix.c_str(), tokens[i][2], postFix2.c_str(), room.c_str());
      Blynk.virtualWrite(V49, tmp);
    }
  }
  if (!deviceFound)
    Serial.printf("tag not found in tokens %d\n", device);
}

/**
 * @brief Runs connectivity checks for all known nodes and backend roster URL.
 *
 * For each entry in `netMap`, performs 4 TCP reachability checks using
 * `isServerConnected()` and writes pass/dead counts to Blynk terminal V49.
 * It then performs 4 HTTP GET checks against `ipList` and reports aggregate
 * pass/dead results.
 */
void ping()
{
  int dead, alive, totalDead = 0, totalPass = 0;
  unsigned long start;
  char line[50], line1[50];

  for (const auto &pair : ipMap)
  {
    alive = dead = 0;
    start = millis();

    String room = pair.second.location.c_str();
    String IP = pair.second.ipAddress.c_str();
    sprintf(line, "%s: %s\n", room.c_str(), IP.c_str());
    for (int j = 0; j < 4; j++)
    {
      if (isServerConnected(IP.c_str()))
        alive++;
      else
        dead++;
    }
    totalPass += alive;
    totalDead += dead;
    sprintf(line1, "\tpass %d dead %d  time: %lu ms\n", alive, dead, millis() - start);
    strcat(line, line1);
    Blynk.virtualWrite(V49, line);
  }
  // ping http
  alive = dead = 0;
  sprintf(line, "%s", ipList);
  start = millis();
  for (int j = 0; j < 4; j++)
  {
    String sensorsConnected = performHttpGet(ipList);
    if (sensorsConnected.isEmpty())
      dead++;
    else
      alive++;
  }
  totalPass += alive;
  totalDead += dead;
  sprintf(line1, "\n\t pass %d dead %d time: %lu ms\n", alive, dead, millis() - start);
  strcat(line, line1);
  Blynk.virtualWrite(V49, line);
  sprintf(line1, "Summary alive: %d dead:%d\n", totalPass, totalDead);
  Blynk.virtualWrite(V49, line1);
}

/**
 * @brief Resolves a sensor mapped to its configured room/location label.
 *
 * Looks up the provided Sensor in `maclocMap`, which is rebuilt in `getSensorData()`
 * from the backend roster payload. Returns an empty string if the sensor is not
 * currently known.
 *
 * @param Sensor (e.g. "BME_x").
 * @return String Room/location text associated with the IP, or "" if missing.
 */
String mac2room(String sensor)
{
  String location = "";
  // Map sensor MAC to room label for user-friendly terminal output.
  auto it = netMap.find(sensor.c_str());
  if (it != netMap.end())
    location = it->second.location.c_str();

  return location;
}

/**
 * @brief Sends a command to one or more sensor nodes selected by Blynk button index.
 *
 * The function maps the incoming widget index to a sensor family label
 * (`ADC`, `BME`, `SHT`, `BMP`, `DS1`, `BMX`, `ALL`), scans `netMap`, and sends
 * the provided command to each matching node via `socketClient()`.
 *
 * @param cmd Null-terminated command string to send (typically "BLK" or "RST").
 * @param index Zero-based Blynk segmented-button index.
 */
void blynkWrite(String cmd, int index)
{
  // Labels must match the Blynk widget button order for virtual pin BLINK_TST (V9/V10):
  // the widget is set in setup() Blynk.setProperty(V9/V10,................

  int elements = menuList->length();
  elements--; // don't count EOL
  Serial.printf("index %d sizeof %d\n", index, elements);

  bool found = false;
  char *str = nullptr;
  // index 7 elements 8
  if (elements < 0 || index >= elements)
  {
    Serial.printf("invalid menu length %d\n", menuList->length());
    return;
  }

  String sensorIndex = menuList[index];

  // Walk every registered sensor; send "BLK/RST" to those matching the selected type (or ALL)
  for (const auto &pair : ipMap)
  {
    if (menuList[index] == "ALL" || strstr(pair.second.location.c_str(), sensorIndex.c_str()))
    {
      found = true;
      str = socketClient((char *)pair.second.ipAddress.c_str(), cmd); // returns heap-allocated C-string
      Serial.printf("Message Recieved from Socket %s \n", str);
      lastMsg = str;
      Blynk.virtualWrite(V47, str);
      free(str);  // release heap buffer returned by socketClient
      lwdtFeed(); // reset watchdog; BLK round-trip can exceed LWD_TIMEOUT on slow nodes
    }
  }
  if (!found)
    Serial.printf("sensor %s not in ip map\n", sensorIndex.c_str());
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
 * @param ip Source IP address for this payload (currently informational).
 * @param mac Source MAC address used to resolve room/location via `maclocMap`.
 */
void processSensorData(float tokens[DEVICES][5], String sensor)
{
  const std::map<int, const char *> sensorMap =
      {
          {78, "BME680"}, //
          {77, "BMP390"},
          {76, "BME280"},
          {58, "BMP280"},
          {44, "SHT35"},
          {48, "ADS1115"},
          {40, "INA219 "},
          {28, "DS1"}};

  String location = mac2room(sensor.c_str());
  if (location.isEmpty())
    Serial.printf("mac address not found\n");

  for (int i = 0; i < 5; i++)
  {
    int sensorCode = static_cast<int>(tokens[i][0]);
    if (!sensorCode)
      break;
    auto it = sensorMap.find(sensorCode);
    if (it != sensorMap.end())
    {
      passSocket++;
      // send to freeRTOS queque
      setupHTTP_request(it->second, location, tokens[i]);
      //   upDateWidget(it->second, tokens[i]);
    }
    else
    {
      Serial.printf("unknow code %d\n", sensorCode);
      continue; // Unknown sensor code
    }
  }
}

/**
 * @brief Queries each unique sensor node for I2C pin mappings and prints results to Blynk terminal.
 *
 * Iterates the prebuilt deduplicated `ipMap` (one entry per node IP),
 * sends the `I2C` socket command to each node, then parses response tuples in the form:
 * `TAG:SDA_PIN,SCL_PIN|` (for example `76:D2(4),D1(5)|`).
 *
 * For every parsed tuple, one formatted line is written to terminal widget V49.
 */
void dumpI2C()
{
  int index, index1, index2;
  char *results, tmp[250];

  // Poll each unique node for its I2C map and stream decoded lines to Blynk terminal.
  for (const auto &pair : ipMap)
  {
    String IP = pair.second.ipAddress.c_str();
    results = socketClient((char *)IP.c_str(), (String) "I2C");
    String data = results;
    while (1)
    {
      // Handle multiple tuples from one node until no ':' delimiter remains.
      // Example payload: 76:D2(4),D1(5)|77:D2(3),D5(0)|
      index = data.indexOf(":");
      if (index == -1)
        break;
      // Parse one tuple and print a human-readable line.
      String i2cAddress = data.substring(0, index);
      index1 = data.indexOf(",");
      String sca = data.substring(index + 1, index1);
      index2 = data.indexOf("|");
      String scl = data.substring(index1 + 1, index2);
      String location = pair.second.location.c_str();
      sprintf(tmp, "Location: %s  I2Caddr :0x%s  \n\t sca:%s scl:%s\n",
              location.c_str(), i2cAddress.c_str(), sca.c_str(), scl.c_str());

      Blynk.virtualWrite(V49, tmp);
      // Move to the next tuple in the stream.
      data = data.substring(index2 + 1);
    }
    free(results);
  }
}
void editLoc()
{
  for (const auto &pair : netMap)
  {
    Serial.printf(" \"%s\" ",
                  pair.second.location.c_str());
  }
  Serial.println();
}
void updateBlynk()
{
  Blynk.virtualWrite(V19, recoveredSocket);
}
String ip2mac(String ip)
{
  String rc = "";
  for (const auto &pair : ipMap)
  {
    if (pair.second.ipAddress == ip.c_str())
    {
      Serial.printf("mac @ %s\n", pair.second.macAddress.c_str());
      return pair.second.macAddress.c_str();
    }
  }
  return rc;
}
void disableTimer()
{
  Serial.println("timer disable");
  timer.disable(timerID1);
}
void enableTimer()
{
  Serial.println("timer enable");
  timer.enable(timerID1);
}