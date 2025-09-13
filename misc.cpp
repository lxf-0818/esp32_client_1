#include <Arduino.h>
#include "time.h"

const char *FAILED_TO_OBTAIN_TIME = "Failed to obtain time";
void getBootTime(char *lastBook, char *strReason);
void get_reset_reason(int reason, char *strReason);

/**
 * @brief Retrieves the boot time and reset reason of the ESP32 device.
 *
 * This function configures the time using an NTP server, retrieves the local time,
 * and formats it into a human-readable string. It also determines the reset reason
 * and provides it as a string. If the time cannot be obtained, it retries up to
 * three times before failing.
 *
 * @param[out] lastBoot A pointer to a character array where the formatted boot time
 *                      will be stored. The array should have enough space to hold
 *                      the formatted string (at least 64 characters).
 * @param[out] strReason A pointer to a character array where the reset reason string
 *                       will be stored.
 *
 * @note The function uses the NTP server "pool.ntp.org" to synchronize the time.
 *       The GMT offset is set to -18000 seconds (UTC-5), and daylight saving time
 *       offset is set to 3600 seconds.
 *
 * @warning Ensure that the `lastBoot` and `strReason` pointers point to valid memory
 *          locations before calling this function.
 */
void getBootTime(char *lastBoot, char *strReason)
{
  const char *ntpServer = "pool.ntp.org";
  const long gmtOffset_sec = -18000;
  const int daylightOffset_sec = 3600;
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;

  int reset_reason = esp_reset_reason();
  get_reset_reason(reset_reason, strReason);
  int retries = 3;

  while (retries--)
  {
    if (!getLocalTime(&timeinfo))
    {
      strncpy(lastBoot, FAILED_TO_OBTAIN_TIME, strlen(FAILED_TO_OBTAIN_TIME) + 1);
      Serial.printf("Failed to obtain time retry: %d\n", 3 - retries);
    }
    else
    {
      int hr = timeinfo.tm_hour;
      snprintf(lastBoot, 64, "%d/%d/%d %d:%02d 0x%02x",
               timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900,
               hr, timeinfo.tm_min, reset_reason);
      break; // passed 1st time!
    }
  }
  Serial.printf("Boot time: %s, Reset reason: %s\n", lastBoot, strReason);
}
/**
 * @brief Converts a reset reason code into a human-readable string.
 *
 * This function takes an integer reset reason code and maps it to a corresponding
 * descriptive string that explains the type of reset that occurred.
 *
 * @param reason The reset reason code (integer) to be converted. Possible values:
 *               - 1: POWERON_RESET - Vbat power on reset
 *               - 3: SW_RESET - Software reset digital core
 *               - 4: OWDT_RESET - Legacy watch dog reset digital core
 *               - 5: DEEPSLEEP_RESET - Deep Sleep reset digital core
 *               - 6: SDIO_RESET - Reset by SLC module, reset digital core
 *               - 7: TG0WDT_SYS_RESET - Timer Group0 Watch dog reset digital core
 *               - 8: TG1WDT_SYS_RESET - Timer Group1 Watch dog reset digital core
 *               - 9: RTCWDT_SYS_RESET - RTC Watch dog Reset digital core
 *               - 10: INTRUSION_RESET - Intrusion tested to reset CPU
 *               - 11: TGWDT_CPU_RESET - Time Group reset CPU
 *               - 12: SW_CPU_RESET - Software reset CPU
 *               - 13: RTCWDT_CPU_RESET - RTC Watch dog Reset CPU
 *               - 14: EXT_CPU_RESET - For APP CPU, reset by PRO CPU
 *               - 15: RTCWDT_BROWN_OUT_RESET - Reset when the VDD voltage is not stable
 *               - 16: RTCWDT_RTC_RESET - RTC Watch dog reset digital core and RTC module
 *               - Any other value: NO_MEAN - No meaningful reset reason
 * @param strReason A pointer to a character array where the human-readable reset reason
 *                  string will be stored. The array must be large enough to hold the
 *                  longest possible string.
 */
void get_reset_reason(int reason, char *strReason)
{
  switch (reason)
  {
  case 1:
    strcpy(strReason, "POWERON_RESET");
    break; /**<1,  Vbat power on reset*/
  case 3:
    strcpy(strReason, "SW_RESET");
    break; /**<3,  Software reset digital core*/
  case 4:
    strcpy(strReason, "OWDT_RESET");
    break; /**<4,  Legacy watch dog reset digital core*/
  case 5:
    strcpy(strReason, "DEEPSLEEP_RESET");
    break; /**<5,  Deep Sleep reset digital core*/
  case 6:
    strcpy(strReason, "SDIO_RESET");
    break; /**<6,  Reset by SLC module, reset digital core*/
  case 7:
    strcpy(strReason, "TG0WDT_SYS_RESET");
    break; /**<7,  Timer Group0 Watch dog reset digital core*/
  case 8:
    strcpy(strReason, "TG1WDT_SYS_RESET");
    break; /**<8,  Timer Group1 Watch dog reset digital core*/
  case 9:
    strcpy(strReason, "RTCWDT_SYS_RESET");
    break; /**<9,  RTC Watch dog Reset digital core*/
  case 10:
    strcpy(strReason, "INTRUSION_RESET");
    break; /**<10, Instrusion tested to reset CPU*/
  case 11:
    strcpy(strReason, "TGWDT_CPU_RESET");
    break; /**<11, Time Group reset CPU*/
  case 12:
    strcpy(strReason, "SW_CPU_RESET");
    break; /**<12, Software reset CPU*/
  case 13:
    strcpy(strReason, "RTCWDT_CPU_RESET");
    break; /**<13, RTC Watch dog Reset CPU*/
  case 14:
    strcpy(strReason, "EXT_CPU_RESET");
    break; /**<14, for APP CPU, reseted by PRO CPU*/
  case 15:
    strcpy(strReason, "RTCWDT_BROWN_OUT_RESET");
    break; /**<15, Reset when the vdd voltage is not stable*/
  case 16:
    strcpy(strReason, "RTCWDT_RTC_RESET");
    break; /**<16, RTC Watch dog reset digital core and rtc module*/
  default:
    strcpy(strReason, "NO_MEAN");
  }
}
