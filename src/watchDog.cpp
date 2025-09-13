// #include <Arduino.h>

// Ticker lwdTicker;
// #define LWD_TIMEOUT 15 * 1000 // Reboot if loop watchdog timer reaches this time out value
// unsigned long lwdTime = 0;
// unsigned long lwdTimeout = LWD_TIMEOUT;

// void lwdtFeed(void);
// void ICACHE_RAM_ATTR lwdtcb(void);
// void lwdtFeed(void)
// {
//   lwdTime = millis();
//   lwdTimeout = lwdTime + LWD_TIMEOUT;
// }
// void ICACHE_RAM_ATTR lwdtcb(void)
// {
//   if ((millis() - lwdTime > LWD_TIMEOUT) || (lwdTimeout - lwdTime != LWD_TIMEOUT))
//   {
//     // Blynk.logEvent("3rd_WDTimer");
//     Serial.printf("3rd_WDTimer esp.restart %lu %lu\n", (millis() - lwdTime), (lwdTimeout - lwdTime));
//     Blynk.virtualWrite(V39, "3rd_WDTimer");
//     queStat();
//     ESP.restart();
//   }
//  }