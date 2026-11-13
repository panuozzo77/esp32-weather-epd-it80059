/* Storage utility implementation for SD card logging and cache management.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include "storage_utils.h"
#include "config.h"

#if defined(NM_EPD_420)
static SPIClass sdSPI(HSPI);
static bool sdMounted = false;
#endif

bool initSD()
{
#if defined(NM_EPD_420)
  if (sdMounted)
  {
    return true;
  }
  sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, sdSPI, 20000000))
  {
    Serial.println("[SD] Mount failed or card not present");
    sdMounted = false;
    return false;
  }
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE)
  {
    Serial.println("[SD] No SD card attached");
    sdMounted = false;
    return false;
  }
  sdMounted = true;
  Serial.printf("[SD] Card initialized. Size: %llu MB\n", SD.cardSize() / (1024 * 1024));
  return true;
#else
  return false;
#endif
}

void powerOffSD()
{
  // Do not call SD.end() before deep sleep; keeping it mounted prevents
  // "sdCommand(): Card Failed!" and the SD card enters low-power standby automatically.
}

bool saveLatestWeather(const weather_bundle_t &bundle)
{
  if (!initSD()) return false;

  if (!SD.exists("/cache"))
  {
    SD.mkdir("/cache");
  }

  File f = SD.open("/cache/latest.json", FILE_WRITE);
  if (!f)
  {
    Serial.println("[SD] Error opening /cache/latest.json for writing");
    return false;
  }

  JsonDocument doc;
  doc["magic"] = bundle.magic;
  doc["last_sync"] = bundle.last_sync_epoch;

  // Hill (Campania Live)
  JsonObject hill = doc["hill"].to<JsonObject>();
  hill["valid"]     = bundle.hill.valid;
  hill["temp"]      = bundle.hill.temp;
  hill["temp_min"]  = bundle.hill.temp_min;
  hill["temp_max"]  = bundle.hill.temp_max;
  hill["humidity"]  = bundle.hill.humidity;
  hill["pressure"]  = bundle.hill.pressure;
  hill["wind_kts"]  = bundle.hill.wind_kts;
  hill["wind_gust"] = bundle.hill.wind_gust_kts;
  hill["wind_dir"]  = bundle.hill.wind_dir;
  hill["rain_today"]= bundle.hill.rain_today;
  hill["last_update"] = bundle.hill.last_update;
  hill["obs_epoch"] = (int64_t)bundle.hill.obs_epoch;

  // Port (Lega Navale)
  JsonObject port = doc["port"].to<JsonObject>();
  port["valid"]     = bundle.port.valid;
  port["temp"]      = bundle.port.temp;
  port["humidity"]  = bundle.port.humidity;
  port["pressure"]  = bundle.port.pressure;
  port["wind_kts"]  = bundle.port.wind_kts;
  port["wind_gust"] = bundle.port.wind_gust_kts;
  port["wind_deg"]  = bundle.port.wind_deg;
  port["wind_dir"]  = bundle.port.wind_dir;
  port["solar"]     = bundle.port.solar_wm2;
  port["uv"]        = bundle.port.uv_index;
  port["rain_today"]= bundle.port.rain_today;
  port["last_update"] = bundle.port.last_update;
  port["obs_epoch"] = (int64_t)bundle.port.obs_epoch;

  // Indoor AHT20
  JsonObject in = doc["indoor"].to<JsonObject>();
  in["valid"]    = bundle.indoor.valid;
  in["temp"]     = bundle.indoor.temp;
  in["humidity"] = bundle.indoor.humidity;

  // Today Forecast
  JsonObject tf = doc["today"].to<JsonObject>();
  tf["valid"]    = bundle.today.valid;
  tf["icon_id"]  = bundle.today.icon_id;
  tf["cond"]     = bundle.today.condition;
  tf["t_min"]    = bundle.today.temp_min;
  tf["t_max"]    = bundle.today.temp_max;
  tf["pop"]      = bundle.today.pop;
  tf["rain_mm"]  = bundle.today.rain_mm;
  tf["w_max"]    = bundle.today.wind_max_kts;
  tf["sunrise"]  = bundle.today.sunrise;
  tf["sunset"]   = bundle.today.sunset;

  serializeJson(doc, f);
  f.close();
  Serial.println("[SD] Cached state saved to /cache/latest.json");
  return true;
}

bool loadLatestWeather(weather_bundle_t &bundle)
{
  if (!initSD()) return false;

  File f = SD.open("/cache/latest.json", FILE_READ);
  if (!f)
  {
    Serial.println("[SD] No /cache/latest.json found");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err)
  {
    Serial.printf("[SD] JSON parse error: %s\n", err.c_str());
    return false;
  }

  bundle.magic = WEATHER_MAGIC_NUMBER;
  bundle.last_sync_epoch = doc["last_sync"] | 0;

  // Hill
  JsonObject hill = doc["hill"];
  bundle.hill.valid         = hill["valid"] | false;
  bundle.hill.temp          = hill["temp"] | NAN;
  bundle.hill.temp_min      = hill["temp_min"] | NAN;
  bundle.hill.temp_max      = hill["temp_max"] | NAN;
  bundle.hill.humidity      = hill["humidity"] | 0;
  bundle.hill.pressure      = hill["pressure"] | NAN;
  bundle.hill.wind_kts      = hill["wind_kts"] | 0.0f;
  bundle.hill.wind_gust_kts = hill["wind_gust"] | 0.0f;
  strlcpy(bundle.hill.wind_dir, hill["wind_dir"] | "--", sizeof(bundle.hill.wind_dir));
  bundle.hill.rain_today    = hill["rain_today"] | 0.0f;
  strlcpy(bundle.hill.last_update, hill["last_update"] | "--:--", sizeof(bundle.hill.last_update));
  bundle.hill.obs_epoch     = (time_t)(hill["obs_epoch"] | (int64_t)0);

  // Port
  JsonObject port = doc["port"];
  bundle.port.valid         = port["valid"] | false;
  bundle.port.temp          = port["temp"] | NAN;
  bundle.port.humidity      = port["humidity"] | 0;
  bundle.port.pressure      = port["pressure"] | NAN;
  bundle.port.wind_kts      = port["wind_kts"] | 0.0f;
  bundle.port.wind_gust_kts = port["wind_gust"] | 0.0f;
  bundle.port.wind_deg      = port["wind_deg"] | 0;
  strlcpy(bundle.port.wind_dir, port["wind_dir"] | "--", sizeof(bundle.port.wind_dir));
  bundle.port.solar_wm2     = port["solar"] | 0.0f;
  bundle.port.uv_index      = port["uv"] | 0;
  bundle.port.rain_today    = port["rain_today"] | 0.0f;
  strlcpy(bundle.port.last_update, port["last_update"] | "--:--", sizeof(bundle.port.last_update));
  bundle.port.obs_epoch     = (time_t)(port["obs_epoch"] | (int64_t)0);

  // Indoor
  JsonObject in = doc["indoor"];
  bundle.indoor.valid    = in["valid"] | false;
  bundle.indoor.temp     = in["temp"] | NAN;
  bundle.indoor.humidity = in["humidity"] | NAN;

  // Today
  JsonObject tf = doc["today"];
  bundle.today.valid        = tf["valid"] | false;
  bundle.today.icon_id      = tf["icon_id"] | 800;
  strlcpy(bundle.today.condition, tf["cond"] | "Sereno", sizeof(bundle.today.condition));
  bundle.today.temp_min     = tf["t_min"] | NAN;
  bundle.today.temp_max     = tf["t_max"] | NAN;
  bundle.today.pop          = tf["pop"] | 0.0f;
  bundle.today.rain_mm      = tf["rain_mm"] | 0.0f;
  bundle.today.wind_max_kts = tf["w_max"] | 0.0f;
  bundle.today.sunrise      = tf["sunrise"] | 0;
  bundle.today.sunset       = tf["sunset"] | 0;

  Serial.println("[SD] Cached state restored successfully");
  return true;
}

bool appendLogCSV(time_t epoch, const weather_bundle_t &bundle)
{
  if (!initSD()) return false;

  if (!SD.exists("/logs"))
  {
    SD.mkdir("/logs");
  }

  tm timeInfo;
  localtime_r(&epoch, &timeInfo);

  char filepath[64];
  snprintf(filepath, sizeof(filepath), "/logs/meteo_%04d_%02d.csv",
           (int)((timeInfo.tm_year + 1900) % 10000), (int)(timeInfo.tm_mon + 1) % 100);

  bool fileExists = SD.exists(filepath);
  File f = SD.open(filepath, FILE_APPEND);
  if (!f)
  {
    Serial.printf("[SD] Failed to open %s for append\n", filepath);
    return false;
  }

  // Header if newly created
  if (!fileExists)
  {
    f.println("epoch,date_time,hill_temp,hill_hum,hill_pres,hill_wind,hill_gust,hill_rain,"
              "port_temp,port_hum,port_wind,port_gust,port_deg,port_solar,port_uv,"
              "in_temp,in_hum");
  }

  char dtStr[32];
  snprintf(dtStr, sizeof(dtStr), "%04d-%02d-%02d %02d:%02d:%02d",
           (int)((timeInfo.tm_year + 1900) % 10000), (int)(timeInfo.tm_mon + 1) % 100, (int)(timeInfo.tm_mday % 100),
           (int)(timeInfo.tm_hour % 100), (int)(timeInfo.tm_min % 100), (int)(timeInfo.tm_sec % 100));

  // Write line
  f.printf("%lld,%s,%.1f,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%d,%.1f,%.1f,%d,%.1f,%d,%.1f,%.1f\n",
           (long long)epoch,
           dtStr,
           bundle.hill.valid ? bundle.hill.temp : NAN,
           bundle.hill.valid ? bundle.hill.humidity : 0,
           bundle.hill.valid ? bundle.hill.pressure : NAN,
           bundle.hill.valid ? bundle.hill.wind_kts : NAN,
           bundle.hill.valid ? bundle.hill.wind_gust_kts : NAN,
           bundle.hill.valid ? bundle.hill.rain_today : 0.0f,
           bundle.port.valid ? bundle.port.temp : NAN,
           bundle.port.valid ? bundle.port.humidity : 0,
           bundle.port.valid ? bundle.port.wind_kts : NAN,
           bundle.port.valid ? bundle.port.wind_gust_kts : NAN,
           bundle.port.valid ? bundle.port.wind_deg : 0,
           bundle.port.valid ? bundle.port.solar_wm2 : 0.0f,
           bundle.port.valid ? bundle.port.uv_index : 0,
           bundle.indoor.valid ? bundle.indoor.temp : NAN,
           bundle.indoor.valid ? bundle.indoor.humidity : NAN);

  f.close();
  Serial.printf("[SD] Appended entry to %s\n", filepath);
  return true;
}

static void appendTrend(weather_bundle_t &bundle, const hourly_sample_t &sample)
{
  if (bundle.trend_count < TREND_MAX_SAMPLES)
  {
    bundle.trend_24h[bundle.trend_count++] = sample;
  }
  else
  {
    memmove(&bundle.trend_24h[0], &bundle.trend_24h[1],
            sizeof(hourly_sample_t) * (TREND_MAX_SAMPLES - 1));
    bundle.trend_24h[TREND_MAX_SAMPLES - 1] = sample;
  }
}

void addTrendSample(weather_bundle_t &bundle, time_t ts)
{
  hourly_sample_t sample;
  sample.timestamp   = ts;
  sample.hill_temp   = bundle.hill.valid ? bundle.hill.temp : NAN;
  sample.port_temp   = bundle.port.valid ? bundle.port.temp : NAN;
  sample.indoor_temp = bundle.indoor.valid ? bundle.indoor.temp : NAN;
  sample.rain_mm     = bundle.port.valid ? bundle.port.rain_today :
                       (bundle.hill.valid ? bundle.hill.rain_today : 0.0f);
  appendTrend(bundle, sample);
}

// CSV columns (see appendLogCSV).
enum {
  CSV_EPOCH = 0, CSV_HILL_TEMP = 2, CSV_HILL_GUST = 6, CSV_HILL_RAIN = 7,
  CSV_PORT_TEMP = 8, CSV_PORT_GUST = 11, CSV_IN_TEMP = 15, CSV_FIELDS = 17
};

// Splits a CSV row into floats; "nan", empty or non-numeric fields become NAN.
static int parseCsvFloats(const char *line, float *out, int maxFields)
{
  int n = 0;
  const char *p = line;
  while (n < maxFields)
  {
    const char *end = strchr(p, ',');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    char buf[24];
    size_t l = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, p, l);
    buf[l] = '\0';
    char *e;
    float v = strtof(buf, &e);
    out[n++] = (e == buf) ? NAN : v;
    if (!end) break;
    p = end + 1;
  }
  for (int i = n; i < maxFields; ++i) out[i] = NAN;
  return n;
}

static const char* dayNamesIt[] = {"Dom", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab"};

bool updateHistoryAndTrend(weather_bundle_t &bundle, const tm *timeInfo)
{
  if (!timeInfo) return false;

  // Initialize history slots for today (index 0) and past 4 days (1..4)
  for (int i = 0; i < 5; ++i)
  {
    time_t targetEpoch = mktime(const_cast<tm*>(timeInfo)) - (i * 86400);
    tm targetTm;
    localtime_r(&targetEpoch, &targetTm);

    snprintf(bundle.history[i].date_str, sizeof(bundle.history[i].date_str), "%02d/%02d",
             (int)(targetTm.tm_mday % 100), (int)(targetTm.tm_mon + 1) % 100);
    strlcpy(bundle.history[i].day_name, dayNamesIt[targetTm.tm_wday % 7], sizeof(bundle.history[i].day_name));
    bundle.history[i].temp_min = 999.0f;
    bundle.history[i].temp_max = -999.0f;
    bundle.history[i].rain_total = 0.0f;
    bundle.history[i].wind_max_kts = 0.0f;
    bundle.history[i].has_data = false;
  }

  // If today's live data is available, populate today's slot (index 0)
  float t_today = bundle.port.valid ? bundle.port.temp : (bundle.hill.valid ? bundle.hill.temp : NAN);
  if (!std::isnan(t_today))
  {
    bundle.history[0].temp_min = bundle.hill.valid && !std::isnan(bundle.hill.temp_min) ? bundle.hill.temp_min : t_today;
    bundle.history[0].temp_max = bundle.hill.valid && !std::isnan(bundle.hill.temp_max) ? bundle.hill.temp_max : t_today;
    bundle.history[0].rain_total = bundle.port.valid ? bundle.port.rain_today : bundle.hill.rain_today;
    bundle.history[0].wind_max_kts = std::max(bundle.port.wind_gust_kts, bundle.hill.wind_gust_kts);
    bundle.history[0].has_data = true;
  }

  // Read the CSV logs from SD to fill the past days and rebuild the 24 h trend
  // (so the trend survives resets and covers a full day).
  if (!initSD()) return false;

  const time_t nowEpoch = mktime(const_cast<tm*>(timeInfo));
  bool trendReset = false;

  // The last 5 days can span the previous month's file.
  for (int back = (timeInfo->tm_mday <= 5) ? 1 : 0; back >= 0; --back)
  {
    int year = timeInfo->tm_year + 1900;
    int mon  = timeInfo->tm_mon + 1 - back;
    if (mon < 1) { mon += 12; --year; }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/logs/meteo_%04d_%02d.csv",
             year % 10000, mon % 100);

    File f = SD.open(filepath, FILE_READ);
    if (!f) continue;

    if (!trendReset)
    {
      bundle.trend_count = 0;
      trendReset = true;
    }

    String line = f.readStringUntil('\n'); // skip header
    while (f.available())
    {
      line = f.readStringUntil('\n');
      line.trim();
      if (line.isEmpty()) continue;

      time_t rowEpoch = (time_t)atoll(line.c_str());
      if (rowEpoch <= 0 || rowEpoch > nowEpoch + 600) continue;

      long age = (long)(nowEpoch - rowEpoch);
      int diffDays = age / 86400;
      if (diffDays >= 5) continue;

      float v[CSV_FIELDS];
      parseCsvFloats(line.c_str(), v, CSV_FIELDS);

      if (age <= 86400)
      {
        hourly_sample_t sample;
        sample.timestamp   = rowEpoch;
        sample.hill_temp   = v[CSV_HILL_TEMP];
        sample.port_temp   = v[CSV_PORT_TEMP];
        sample.indoor_temp = v[CSV_IN_TEMP];
        sample.rain_mm     = std::isnan(v[CSV_HILL_RAIN]) ? 0.0f : v[CSV_HILL_RAIN];
        appendTrend(bundle, sample);
      }

      float gust = 0.0f;
      if (!std::isnan(v[CSV_HILL_GUST])) gust = std::max(gust, v[CSV_HILL_GUST]);
      if (!std::isnan(v[CSV_PORT_GUST])) gust = std::max(gust, v[CSV_PORT_GUST]);
      float rain = std::isnan(v[CSV_HILL_RAIN]) ? 0.0f : v[CSV_HILL_RAIN];

      float bestTemp = !std::isnan(v[CSV_PORT_TEMP]) ? v[CSV_PORT_TEMP] : v[CSV_HILL_TEMP];
      if (!std::isnan(bestTemp))
      {
        daily_summary_t &d = bundle.history[diffDays];
        d.temp_min = std::min(d.temp_min, bestTemp);
        d.temp_max = std::max(d.temp_max, bestTemp);
        d.rain_total = std::max(d.rain_total, rain);
        d.wind_max_kts = std::max(d.wind_max_kts, gust);
        d.has_data = true;
      }
    }
    f.close();
  }

  // Clean up any slots that had no data
  for (int i = 0; i < 5; ++i)
  {
    if (!bundle.history[i].has_data || bundle.history[i].temp_min > 900.0f)
    {
      bundle.history[i].temp_min = NAN;
      bundle.history[i].temp_max = NAN;
      bundle.history[i].has_data = false;
    }
  }

  return true;
}
