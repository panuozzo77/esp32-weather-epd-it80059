/* Web scraper implementation for Campania Live and Lega Navale Torre del Greco.
 */

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "scraper_utils.h"
#include "api_response.h"
#include "conversions.h"
#include "html_extract.h"

static const char *SCRAPER_USER_AGENT = "esp32-weather-epd/1.0";
static const uint16_t SCRAPER_CONNECT_TIMEOUT_MS = 8000;
static const uint16_t SCRAPER_READ_TIMEOUT_MS    = 10000;
static const size_t   SCRAPER_MAX_BYTES          = 400 * 1024;

static const char *CAMPANIALIVE_HOST = "www.campanialive.it";
static const char *CAMPANIALIVE_PATH = "/dati-meteo.asp?stazione=torredelgreco";

static const char *LEGANAVALE_HOST = "www.leganavaletorredelgreco.it";
static const char *LEGANAVALE_PATH = "/meteo/";

// Downloads host/path and feeds the body to the extractor until every field
// has been found (the useful data sits near the top of the page, so most of
// the download is skipped).
//
// HTTP/1.0 is used on purpose: with HTTP/1.1 servers reply with
// "Transfer-Encoding: chunked" and HTTPClient::getStream() hands back the raw
// socket, so chunk-size lines would be interleaved with the HTML and could
// split a marker or corrupt a number.
static bool fetchFields(const char *host, const char *path, bool https,
                        HtmlFieldExtractor &extractor)
{
  WiFiClient       plainClient;
  WiFiClientSecure secureClient;
  HTTPClient       http;

  http.useHTTP10(true);
  http.setReuse(false);
  http.setConnectTimeout(SCRAPER_CONNECT_TIMEOUT_MS);
  http.setTimeout(SCRAPER_READ_TIMEOUT_MS);
  http.setUserAgent(SCRAPER_USER_AGENT);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  bool begun;
  if (https)
  {
    // Public weather page, no credentials involved: certificate pinning would
    // only add a periodic manual update when the site's cert rotates.
    secureClient.setInsecure();
    begun = http.begin(secureClient, host, 443, path, true);
  }
  else
  {
    begun = http.begin(plainClient, host, 80, path);
  }
  if (!begun)
  {
    Serial.printf("[Scraper] %s: begin() failed\n", host);
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("[Scraper] %s%s (%s): HTTP %d %s\n", host, path,
                  https ? "https" : "http", httpCode,
                  httpCode < 0 ? http.errorToString(httpCode).c_str() : "");
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  int remaining = http.getSize(); // -1 when no Content-Length
  size_t total = 0;
  unsigned long lastData = millis();
  char buf[512];

  extractor.reset();
  while (!extractor.allFound() && total < SCRAPER_MAX_BYTES)
  {
    size_t avail = stream->available();
    if (avail == 0)
    {
      if (!stream->connected()) break; // server closed, body complete
      if (millis() - lastData > SCRAPER_READ_TIMEOUT_MS)
      {
        Serial.printf("[Scraper] %s: read timeout after %u bytes\n", host, (unsigned)total);
        break;
      }
      delay(5);
      continue;
    }
    int n = stream->read((uint8_t *)buf, avail < sizeof(buf) ? avail : sizeof(buf));
    if (n <= 0) continue;
    extractor.feed(buf, n);
    total += n;
    lastData = millis();
    if (remaining > 0 && (remaining -= n) <= 0) break;
  }

  Serial.printf("[Scraper] %s: read %u bytes, all fields found: %s\n",
                host, (unsigned)total, extractor.allFound() ? "yes" : "no");
  http.end();
  return true;
}

// Sets the station's observation time from the page's own timestamp, so a
// station that stopped uploading is not presented as current. Falls back to
// the fetch time if the timestamp cannot be parsed.
static void setObservationTime(const html_field_t &f, local_station_t &st)
{
  int y, mo, d, h, mi;
  time_t obs = 0;
  if (f.found && htmlParseDateTime(f.value, y, mo, d, h, mi))
  {
    tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon  = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min  = mi;
    t.tm_isdst = -1; // let the TZ rules decide
    obs = mktime(&t);
  }
  else
  {
    Serial.printf("[Scraper] Could not parse station timestamp '%s'\n", f.value);
  }
  if (obs <= 0) obs = time(nullptr);

  st.obs_epoch = obs;
  tm tmInfo;
  localtime_r(&obs, &tmInfo);
  snprintf(st.last_update, sizeof(st.last_update), "%02d/%02d %02d:%02d",
           tmInfo.tm_mday, tmInfo.tm_mon + 1, tmInfo.tm_hour, tmInfo.tm_min);

  long age = (long)(time(nullptr) - obs);
  if (age > STATION_STALE_SECONDS)
  {
    Serial.printf("[Scraper] Station data is stale: last observation %s (%ld min ago)\n",
                  st.last_update, age / 60);
  }
}

// Parses a numeric field and checks it against a plausible range, so that a
// placeholder ("--") or a value grabbed from the wrong place is rejected
// instead of being shown as e.g. 0 C.
static bool parseRange(const html_field_t &f, float lo, float hi, float &out)
{
  float v;
  if (!f.found || !htmlParseNumber(f.value, v)) return false;
  if (v < lo || v > hi)
  {
    Serial.printf("[Scraper] Rejecting out-of-range value '%s' for '%s'\n",
                  f.value, f.anchor);
    return false;
  }
  out = v;
  return true;
}

// Scrape Campania Live (Torre del Greco station, 154 m)
bool fetchCampaniaLive(WiFiClient &client, local_station_t &hill)
{
  (void)client;
  Serial.println("[Scraper] Connecting to Campania Live (Torre del Greco 154m)...");

  enum { F_TEMP, F_TMAX, F_TMIN, F_HUM, F_PRESS, F_WIND, F_GUST, F_RAIN, F_UPD, F_COUNT };
  html_field_t f[F_COUNT] = {};
  f[F_TEMP]  = { "Temperatura attuale<", "<font" };
  f[F_TMAX]  = { "Temperatura massima<", "<font" };
  f[F_TMIN]  = { "Temperatura minima<", "<font" };
  f[F_HUM]   = { "scope=\"row\">Umidit", "<font" };
  f[F_PRESS] = { "Pressione sul livello del mare<", "<font" };
  f[F_WIND]  = { "Vento medio ultimi 10 minuti<", "<font" };
  f[F_GUST]  = { "Raffica massima di oggi<", "<font" };
  f[F_RAIN]  = { "Pioggia oggi<", "<font" };
  f[F_UPD]   = { "Ultimo rilevamento:", "" }; // "11&nbsp;novembre&nbsp;2026 ore 23:40"
  HtmlFieldExtractor extractor(f, F_COUNT);

  bool ok = fetchFields(CAMPANIALIVE_HOST, CAMPANIALIVE_PATH, false, extractor)
            && f[F_TEMP].found;
  if (!ok)
  {
    Serial.println("[Scraper] Campania Live: retrying...");
    delay(1000);
    ok = fetchFields(CAMPANIALIVE_HOST, CAMPANIALIVE_PATH, false, extractor)
         && f[F_TEMP].found;
  }

  // Parse into a scratch copy: on failure the last good values are kept.
  local_station_t st = hill;
  st.temp = st.temp_min = st.temp_max = st.feels_like = NAN;
  st.humidity = 0;
  st.pressure = NAN;
  st.wind_kts = st.wind_gust_kts = 0.0f;
  st.rain_today = 0.0f;
  strlcpy(st.wind_dir, "--", sizeof(st.wind_dir));

  float v;
  if (!ok || !parseRange(f[F_TEMP], -30, 55, st.temp))
  {
    Serial.println("[Scraper] Campania Live: no valid temperature, keeping previous data");
    return false;
  }
  parseRange(f[F_TMAX], -30, 55, st.temp_max);
  parseRange(f[F_TMIN], -30, 55, st.temp_min);
  if (parseRange(f[F_HUM], 0, 100, v)) st.humidity = (int)lroundf(v);
  parseRange(f[F_PRESS], 900, 1100, st.pressure);
  parseRange(f[F_WIND], 0, 150, st.wind_kts);
  parseRange(f[F_GUST], 0, 200, st.wind_gust_kts);
  parseRange(f[F_RAIN], 0, 500, st.rain_today);
  if (!htmlParseWord(f[F_WIND].value, "direz.", st.wind_dir, sizeof(st.wind_dir)))
  {
    strlcpy(st.wind_dir, "--", sizeof(st.wind_dir));
  }

  Serial.printf("[Scraper] Campania Live: T %.1f C (%.1f/%.1f), Hum %d %%, "
                "P %.1f hPa, Wind %.1f kts %s, Gust %.1f kts, Rain %.1f mm\n",
                st.temp, st.temp_min, st.temp_max, st.humidity,
                st.pressure, st.wind_kts, st.wind_dir,
                st.wind_gust_kts, st.rain_today);

  st.valid = true;
  setObservationTime(f[F_UPD], st);
  hill = st;
  return true;
}

// Scrape Lega Navale Torre del Greco (Ecowitt WordPress plugin page)
bool fetchLegaNavale(WiFiClient &client, local_station_t &port)
{
  (void)client;
  Serial.println("[Scraper] Connecting to Lega Navale (Torre del Greco Pontile)...");

  enum { F_TEMP, F_WIND, F_GUST, F_PRESS, F_DEG, F_DIR, F_HUM, F_FEELS,
         F_SOLAR, F_UV, F_RAIN, F_UPD, F_COUNT };
  html_field_t f[F_COUNT] = {};
  f[F_TEMP]  = { "data-ews-field=\"temperature\"", nullptr };
  f[F_WIND]  = { "data-ews-field=\"wind_speed\"", nullptr };
  f[F_GUST]  = { "data-ews-field=\"wind_gust\"", nullptr };
  f[F_PRESS] = { "data-ews-field=\"pressure\"", nullptr };
  f[F_DEG]   = { "ews-compass-deg\"", nullptr };
  f[F_DIR]   = { "ews-compass-cardinal\"", nullptr };
  f[F_HUM]   = { "Umidit\xC3\xA0 al pontile<", "class=\"ecowitt-value\"" };
  f[F_FEELS] = { "Percepita (pontile)<", "class=\"ecowitt-value\"" };
  f[F_SOLAR] = { ">Irradianza solare<", "class=\"ecowitt-value\"" };
  f[F_UV]    = { ">Indice UV<", "class=\"ecowitt-value\"" };
  f[F_RAIN]  = { ">Oggi<", "class=\"ecowitt-value\"" };
  f[F_UPD]   = { ">Aggiornamento<", "class=\"value\"" }; // "Novembre 13, 2026 9:25 am"
  HtmlFieldExtractor extractor(f, F_COUNT);

  // Plain HTTP first (less RAM and time than TLS), then HTTPS.
  bool ok = fetchFields(LEGANAVALE_HOST, LEGANAVALE_PATH, false, extractor)
            && f[F_TEMP].found;
  if (!ok)
  {
    Serial.println("[Scraper] Lega Navale: retrying over HTTPS...");
    ok = fetchFields(LEGANAVALE_HOST, LEGANAVALE_PATH, true, extractor)
         && f[F_TEMP].found;
  }

  // Parse into a scratch copy: on failure the last good values are kept.
  local_station_t st = port;
  st.temp = st.temp_min = st.temp_max = st.feels_like = NAN;
  st.humidity = 0;
  st.pressure = NAN;
  st.wind_kts = st.wind_gust_kts = 0.0f;
  st.wind_deg = 0;
  st.solar_wm2 = 0.0f;
  st.uv_index = 0;
  st.rain_today = 0.0f;
  strlcpy(st.wind_dir, "--", sizeof(st.wind_dir));

  float v;
  if (!ok || !parseRange(f[F_TEMP], -30, 55, st.temp))
  {
    Serial.println("[Scraper] Lega Navale: no valid temperature, keeping previous data");
    return false;
  }
  parseRange(f[F_FEELS], -40, 65, st.feels_like);
  parseRange(f[F_WIND], 0, 150, st.wind_kts);
  parseRange(f[F_GUST], 0, 200, st.wind_gust_kts);
  parseRange(f[F_PRESS], 900, 1100, st.pressure);
  if (parseRange(f[F_DEG], 0, 360, v))  st.wind_deg = (int)lroundf(v) % 360;
  if (parseRange(f[F_HUM], 0, 100, v))  st.humidity = (int)lroundf(v);
  parseRange(f[F_SOLAR], 0, 2000, st.solar_wm2);
  if (parseRange(f[F_UV], 0, 20, v))    st.uv_index = (int)lroundf(v);
  parseRange(f[F_RAIN], 0, 500, st.rain_today);
  if (!htmlParseWord(f[F_DIR].value, nullptr, st.wind_dir, sizeof(st.wind_dir)))
  {
    strlcpy(st.wind_dir, "--", sizeof(st.wind_dir));
  }

  Serial.printf("[Scraper] Lega Navale: T %.1f C, Hum %d %%, P %.1f hPa, "
                "Wind %.1f/%.1f kts %s (%d deg), Solar %.1f, UV %d, Rain %.1f mm\n",
                st.temp, st.humidity, st.pressure, st.wind_kts,
                st.wind_gust_kts, st.wind_dir, st.wind_deg,
                st.solar_wm2, st.uv_index, st.rain_today);

  st.valid = true;
  setObservationTime(f[F_UPD], st);
  port = st;
  return true;
}

// Populate today's forecast from OWM data
void populateTodayForecastFromOWM(const void *owm_onecall_ptr, today_forecast_t &today)
{
  if (!owm_onecall_ptr) return;
  const owm_resp_onecall_t &onecall = *static_cast<const owm_resp_onecall_t*>(owm_onecall_ptr);

  const owm_daily_t &d0 = onecall.daily[0];
  today.valid = true;
  today.icon_id = d0.weather.id;
  strlcpy(today.condition, d0.weather.description.c_str(), sizeof(today.condition));

  // Convert from Kelvin to Celsius
  today.temp_min = (float)std::round((kelvin_to_celsius(d0.temp.min)) * 10) / 10.0f;
  today.temp_max = (float)std::round((kelvin_to_celsius(d0.temp.max)) * 10) / 10.0f;
  today.pop      = d0.pop * 100.0f; // percentage 0-100
  today.rain_mm  = d0.rain;
  today.wind_max_kts = meterspersecond_to_knots(d0.wind_speed);
  today.sunrise  = d0.sunrise;
  today.sunset   = d0.sunset;

  Serial.printf("[Forecast] Today: Min %.1f °C, Max %.1f °C, PoP %.0f %%, Cond: %s\n",
                today.temp_min, today.temp_max, today.pop, today.condition);
}
