/* Main program for esp32-weather-epd.
 * Multi-station monitor for Torre del Greco (Collina 154m + Porto Pontile + Casa AHT20)
 * with SD card logging, offline fallback, and multi-screen navigation.
 */

#include "config.h"
#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <Preferences.h>
#include <time.h>
#include <WiFi.h>
#include <Wire.h>

#include "_locale.h"
#include "api_response.h"
#include "client_utils.h"
#include "display_utils.h"
#include "renderer.h"
#include "station_data.h"
#include "storage_utils.h"
#include "scraper_utils.h"

#ifdef DISP_3C_E420
  #include "icons/icons_96x96.h"
#else
  #include "icons/icons_196x196.h"
#endif

#ifdef USE_OPENWEATHER_MOCK_DATA
  #include "mock_weather.h"
#endif

#if defined(SENSOR_AHT20)
  #include <Adafruit_AHTX0.h>
#endif
#if defined(USE_HTTPS_WITH_CERT_VERIF) || defined(USE_HTTPS_NO_CERT_VERIF)
  #include <WiFiClientSecure.h>
#endif
#ifdef USE_HTTPS_WITH_CERT_VERIF
  #include "cert.h"
#endif

// Persist bundle across Deep Sleep cycles in RTC Slow Memory (<1 KB)
RTC_DATA_ATTR weather_bundle_t rtc_bundle;

// OpenWeather structures for today's forecast and mock data
static owm_resp_onecall_t       owm_onecall;
static owm_resp_air_pollution_t owm_air_pollution;

Preferences prefs;

/* Reads on-board AHT20 environment sensor
 */
bool readIndoorSensor(float &inTemp, float &inHumidity)
{
  pinMode(PIN_SENSOR_PWR, OUTPUT);
  digitalWrite(PIN_SENSOR_PWR, HIGH);
  delay(100); // AHT20 power-on stabilization delay

  TwoWire I2C_sensor = TwoWire(0);
  I2C_sensor.begin(PIN_SENSOR_SDA, PIN_SENSOR_SCL, 100000);

  bool sensorFound = false;
  inTemp     = NAN;
  inHumidity = NAN;

#if defined(SENSOR_AHT20)
  Adafruit_AHTX0 aht;
  I2C_sensor.beginTransmission(AHT20_ADDRESS);
  if (I2C_sensor.endTransmission() == 0)
  {
    delay(20); // Small settling delay after bus probe
    sensorFound = aht.begin(&I2C_sensor);
  }
  if (sensorFound)
  {
    sensors_event_t humidityEvent, temperatureEvent;
    if (aht.getEvent(&humidityEvent, &temperatureEvent))
    {
      inTemp     = temperatureEvent.temperature;
      inHumidity = humidityEvent.relative_humidity;
      Serial.printf("[Sensor] AHT20 Temp: %.1f \260C, Hum: %.1f %%\n", inTemp, inHumidity);
    }
  }
  else
  {
    Serial.println("[Sensor] AHT20 begin failed or not found");
  }
#endif

  digitalWrite(PIN_SENSOR_PWR, LOW);
  return sensorFound && !std::isnan(inTemp) && !std::isnan(inHumidity);
}

/* Put esp32 into ultra low-power deep sleep (<11μA).
 * Aligns wake time to the minute. Configures hardware button interrupts.
 */
void beginDeepSleep(unsigned long startTime, tm *timeInfo)
{
  if (!getLocalTime(timeInfo))
  {
    Serial.println(TXT_REFERENCING_OLDER_TIME_NOTICE);
  }

  int bedtimeHour = INT_MAX;
  if (BED_TIME != WAKE_TIME)
  {
    bedtimeHour = (BED_TIME - WAKE_TIME + 24) % 24;
  }

  int curHour = (timeInfo->tm_hour - WAKE_TIME + 24) % 24;
  const int curMinute = curHour * 60 + timeInfo->tm_min;
  const int curSecond = curHour * 3600 + timeInfo->tm_min * 60 + timeInfo->tm_sec;
  const int desiredSleepSeconds = SLEEP_DURATION * 60;
  const int offsetMinutes = curMinute % SLEEP_DURATION;
  const int offsetSeconds = curSecond % desiredSleepSeconds;

  int sleepMinutes = SLEEP_DURATION - offsetMinutes;
  if (desiredSleepSeconds - offsetSeconds < 120
   || offsetSeconds / (float)desiredSleepSeconds > 0.95f)
  {
    sleepMinutes += SLEEP_DURATION;
  }

  const int predictedWakeHour = ((curMinute + sleepMinutes) / 60) % 24;

  uint64_t sleepDuration;
  if (predictedWakeHour < bedtimeHour)
  {
    sleepDuration = sleepMinutes * 60 - timeInfo->tm_sec;
  }
  else
  {
    const int hoursUntilWake = 24 - curHour;
    sleepDuration = hoursUntilWake * 3600ULL - (timeInfo->tm_min * 60ULL + timeInfo->tm_sec);
  }

  sleepDuration += 3ULL;
  sleepDuration *= 1.0015f;

#if DEBUG_LEVEL >= 1
  printHeapUsage();
#endif

  // Configure hardware button wakeup: BOOT (GPIO 0 is an RTC IO on ESP32-S3)
#if defined(NM_EPD_420)
  pinMode(PIN_KEY_BOOT, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_KEY_BOOT, 0); // 0 = LOW
#endif

  esp_sleep_enable_timer_wakeup(sleepDuration * 1000000ULL);
  Serial.print(TXT_AWAKE_FOR);
  Serial.println(" " + String((millis() - startTime) / 1000.0, 3) + "s");
  Serial.print(TXT_ENTERING_DEEP_SLEEP_FOR);
  Serial.println(" " + String(sleepDuration) + "s");
  esp_deep_sleep_start();
}

/* Program entry point.
 */
void setup()
{
  unsigned long startTime = millis();
  Serial.begin(115200);

#if DEBUG_LEVEL >= 1
  printHeapUsage();
#endif

  disableBuiltinLED();

  // 1. Check Wakeup Reason
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  bool isExt0Wakeup = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0);
  bool isButtonPressed = false;
  bool isLongPress = false;

#if defined(NM_EPD_420)
  pinMode(PIN_KEY_BOOT, INPUT_PULLUP);
  pinMode(PIN_KEY_USER, INPUT_PULLUP);

  if (isExt0Wakeup || digitalRead(PIN_KEY_BOOT) == LOW)
  {
    isButtonPressed = true;
    unsigned long pressStart = millis();
    while (digitalRead(PIN_KEY_BOOT) == LOW && (millis() - pressStart) < 1200)
    {
      delay(20);
    }
    isLongPress = (millis() - pressStart) >= 1200;
  }
  else if (digitalRead(PIN_KEY_USER) == LOW)
  {
    isButtonPressed = true;
    isLongPress = false;
  }
#endif

  // 2. FAST PATH: Short press on BOOT (or USER) -> Cycle page immediately without WiFi
  if (isButtonPressed && !isLongPress && (rtc_bundle.magic == WEATHER_MAGIC_NUMBER))
  {
    Serial.println("[Wakeup] Button clicked -> cycling screen without WiFi...");
    rtc_bundle.current_page = (rtc_bundle.current_page + 1) % 3;

    // Refresh indoor AHT20 sensor
    float inTemp = NAN, inHum = NAN;
    if (readIndoorSensor(inTemp, inHum))
    {
      rtc_bundle.indoor.temp = inTemp;
      rtc_bundle.indoor.humidity = inHum;
      rtc_bundle.indoor.valid = true;
    }

    tm timeInfo = {};
    getLocalTime(&timeInfo);

    renderWeatherBundle(rtc_bundle, timeInfo);
    beginDeepSleep(startTime, &timeInfo);
    return;
  }

  // 3. FULL PATH: Timer wakeup, long press, or initial Boot
  Serial.println("[Wakeup] Full update cycle initiated...");

  // Initialize NVS
  prefs.begin(NVS_NAMESPACE, false);

#if BATTERY_MONITORING
  uint32_t batteryVoltage = readBatteryVoltage();
  Serial.print(TXT_BATTERY_VOLTAGE);
  Serial.println(": " + String(batteryVoltage) + "mv");

  bool lowBat = prefs.getBool("lowBat", false);
  if (batteryVoltage <= LOW_BATTERY_VOLTAGE)
  {
    if (!lowBat)
    {
      prefs.putBool("lowBat", true);
      prefs.end();
      initDisplay();
      do
      {
        #ifdef DISP_3C_E420
        drawError(battery_alert_0deg_96x96, TXT_LOW_BATTERY);
        #else
        drawError(battery_alert_0deg_196x196, TXT_LOW_BATTERY);
        #endif
      } while (display.nextPage());
      powerOffDisplay();
    }
    esp_deep_sleep_start();
  }
  if (lowBat) prefs.putBool("lowBat", false);
#else
  uint32_t batteryVoltage = 0;
#endif

  prefs.end();

  // If first boot or RTC memory invalid, restore cached state from SD
  initSD();
  if (rtc_bundle.magic != WEATHER_MAGIC_NUMBER)
  {
    Serial.println("[Init] Initializing bundle from SD cache / defaults...");
    memset(&rtc_bundle, 0, sizeof(rtc_bundle));
    rtc_bundle.magic = WEATHER_MAGIC_NUMBER;
    rtc_bundle.current_page = 0;
    loadLatestWeather(rtc_bundle);
  }

  tm timeInfo = {};
  int wifiRSSI = 0;
  wl_status_t wifiStatus = startWiFi(wifiRSSI);

  if (wifiStatus == WL_CONNECTED)
  {
    // Synchronize NTP Time
    configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
    waitForSNTPSync(&timeInfo);

    // Fetch Weather Data from both local stations with fresh independent clients
    {
      WiFiClient client1;
      fetchCampaniaLive(client1, rtc_bundle.hill);
    }

    delay(200); // Allow previous socket and lwIP TCP buffers to fully settle

    {
      WiFiClient client2;
      fetchLegaNavale(client2, rtc_bundle.port);
    }

    // Fetch Today's Forecast from OWM or Mock
#ifdef USE_OPENWEATHER_MOCK_DATA
    loadMockOpenWeatherData(owm_onecall, owm_air_pollution);
    populateTodayForecastFromOWM(&owm_onecall, rtc_bundle.today);
#else
    {
      WiFiClientSecure secureClient3;
      secureClient3.setInsecure();
      int rxStatus = getOWMonecall(secureClient3, owm_onecall);
      if (rxStatus == HTTP_CODE_OK)
      {
        populateTodayForecastFromOWM(&owm_onecall, rtc_bundle.today);
      }
    }
#endif

    killWiFi();

    rtc_bundle.is_offline = false;
    rtc_bundle.last_sync_epoch = time(nullptr);
    rtc_bundle.wifi_rssi = wifiRSSI;

    // Persist to SD Card
    saveLatestWeather(rtc_bundle);
    appendLogCSV(rtc_bundle.last_sync_epoch, rtc_bundle);
    addTrendSample(rtc_bundle, rtc_bundle.last_sync_epoch);
    updateHistoryAndTrend(rtc_bundle, &timeInfo);
  }
  else
  {
    // Offline Fallback
    Serial.println("[WiFi] Connection failed. Entering OFFLINE mode with cached data.");
    killWiFi();
    rtc_bundle.is_offline = true;
    getLocalTime(&timeInfo);
  }

  // Read Internal AHT20 Sensor
  float inTemp = NAN, inHumidity = NAN;
  bool sensorFound = readIndoorSensor(inTemp, inHumidity);
  rtc_bundle.indoor.temp     = inTemp;
  rtc_bundle.indoor.humidity = inHumidity;
  rtc_bundle.indoor.valid    = sensorFound;
  rtc_bundle.battery_mv      = batteryVoltage;

  // Render display (never blanks screen, shows cached data with OFFLINE badge if disconnected)
  renderWeatherBundle(rtc_bundle, timeInfo);

  // Enter deep sleep
  beginDeepSleep(startTime, &timeInfo);
}

void loop()
{
}
