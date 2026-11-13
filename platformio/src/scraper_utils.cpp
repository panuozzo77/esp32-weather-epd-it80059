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

// Scrape Campania Live (Torre del Greco station, 154 m)
bool fetchCampaniaLive(WiFiClient &client, local_station_t &hill)
{
  (void)client;
  Serial.println("[Scraper] Connecting to Campania Live (Torre del Greco 154m)...");

  enum { F_TEMP, F_TMAX, F_TMIN, F_HUM, F_PRESS, F_WIND, F_GUST, F_RAIN, F_COUNT };
  html_field_t f[F_COUNT] = {};
  f[F_TEMP]  = { "Temperatura attuale<", "<font" };
  f[F_TMAX]  = { "Temperatura massima<", "<font" };
  f[F_TMIN]  = { "Temperatura minima<", "<font" };
  f[F_HUM]   = { "scope=\"row\">Umidit", "<font" };
  f[F_PRESS] = { "Pressione sul livello del mare<", "<font" };
  f[F_WIND]  = { "Vento medio ultimi 10 minuti<", "<font" };
  f[F_GUST]  = { "Raffica massima di oggi<", "<font" };
  f[F_RAIN]  = { "Pioggia oggi<", "<font" };
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

  hill.temp = NAN;
  hill.temp_min = NAN;
  hill.temp_max = NAN;
  hill.humidity = 0;
  hill.pressure = 0.0f;
  hill.wind_kts = 0.0f;
  hill.wind_gust_kts = 0.0f;
  hill.rain_today = 0.0f;
  strlcpy(hill.wind_dir, "--", sizeof(hill.wind_dir));

  float v;
  if (htmlParseNumber(f[F_TEMP].value, v))  hill.temp = v;
  if (htmlParseNumber(f[F_TMAX].value, v))  hill.temp_max = v;
  if (htmlParseNumber(f[F_TMIN].value, v))  hill.temp_min = v;
  if (htmlParseNumber(f[F_HUM].value, v))   hill.humidity = (int)lroundf(v);
  if (htmlParseNumber(f[F_PRESS].value, v)) hill.pressure = v;
  if (htmlParseNumber(f[F_WIND].value, v))  hill.wind_kts = v;
  if (htmlParseNumber(f[F_GUST].value, v))  hill.wind_gust_kts = v;
  if (htmlParseNumber(f[F_RAIN].value, v))  hill.rain_today = v;
  htmlParseWord(f[F_WIND].value, "direz.", hill.wind_dir, sizeof(hill.wind_dir));

  Serial.printf("[Scraper] Campania Live: T %.1f C (%.1f/%.1f), Hum %d %%, "
                "P %.1f hPa, Wind %.1f kts %s, Gust %.1f kts, Rain %.1f mm\n",
                hill.temp, hill.temp_min, hill.temp_max, hill.humidity,
                hill.pressure, hill.wind_kts, hill.wind_dir,
                hill.wind_gust_kts, hill.rain_today);

  hill.valid = !std::isnan(hill.temp);
  if (hill.valid)
  {
    time_t now = time(nullptr);
    tm tmInfo;
    localtime_r(&now, &tmInfo);
    snprintf(hill.last_update, sizeof(hill.last_update), "%02d:%02d", tmInfo.tm_hour, tmInfo.tm_min);
  }
  else
  {
    Serial.println("[Scraper] Campania Live: temperature not found");
  }
  return hill.valid;
}

// Scrape Lega Navale Torre del Greco (Ecowitt WordPress plugin page)
bool fetchLegaNavale(WiFiClient &client, local_station_t &port)
{
  (void)client;
  Serial.println("[Scraper] Connecting to Lega Navale (Torre del Greco Pontile)...");

  enum { F_TEMP, F_WIND, F_GUST, F_PRESS, F_DEG, F_DIR, F_HUM, F_FEELS,
         F_SOLAR, F_UV, F_RAIN, F_COUNT };
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

  port.temp = NAN;
  port.temp_min = NAN;
  port.temp_max = NAN;
  port.feels_like = NAN;
  port.humidity = 0;
  port.pressure = 0.0f;
  port.wind_kts = 0.0f;
  port.wind_gust_kts = 0.0f;
  port.wind_deg = 0;
  port.solar_wm2 = 0.0f;
  port.uv_index = 0;
  port.rain_today = 0.0f;
  strlcpy(port.wind_dir, "--", sizeof(port.wind_dir));

  float v;
  if (htmlParseNumber(f[F_TEMP].value, v))  port.temp = v;
  if (htmlParseNumber(f[F_FEELS].value, v)) port.feels_like = v;
  if (htmlParseNumber(f[F_WIND].value, v))  port.wind_kts = v;
  if (htmlParseNumber(f[F_GUST].value, v))  port.wind_gust_kts = v;
  if (htmlParseNumber(f[F_PRESS].value, v)) port.pressure = v;
  if (htmlParseNumber(f[F_DEG].value, v))   port.wind_deg = (int)lroundf(v);
  if (htmlParseNumber(f[F_HUM].value, v))   port.humidity = (int)lroundf(v);
  if (htmlParseNumber(f[F_SOLAR].value, v)) port.solar_wm2 = v;
  if (htmlParseNumber(f[F_UV].value, v))    port.uv_index = (int)lroundf(v);
  if (htmlParseNumber(f[F_RAIN].value, v))  port.rain_today = v;
  htmlParseWord(f[F_DIR].value, nullptr, port.wind_dir, sizeof(port.wind_dir));

  Serial.printf("[Scraper] Lega Navale: T %.1f C, Hum %d %%, P %.1f hPa, "
                "Wind %.1f/%.1f kts %s (%d deg), Solar %.1f, UV %d, Rain %.1f mm\n",
                port.temp, port.humidity, port.pressure, port.wind_kts,
                port.wind_gust_kts, port.wind_dir, port.wind_deg,
                port.solar_wm2, port.uv_index, port.rain_today);

  port.valid = !std::isnan(port.temp);
  if (port.valid)
  {
    time_t now = time(nullptr);
    tm tmInfo;
    localtime_r(&now, &tmInfo);
    snprintf(port.last_update, sizeof(port.last_update), "%02d:%02d", tmInfo.tm_hour, tmInfo.tm_min);
  }
  else
  {
    Serial.println("[Scraper] Lega Navale: temperature not found");
  }
  return port.valid;
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
