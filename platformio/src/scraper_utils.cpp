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

static const char *LEGANAVALE_HOST = "www.leganavaletorredelgreco.it";
static const char *LEGANAVALE_PATH = "/meteo/";

// Helper: skips non-numeric characters until digit or '-' or '+'
static bool skipToNumber(Stream &s, unsigned long timeoutMs = 2000)
{
  unsigned long start = millis();
  while (millis() - start < timeoutMs)
  {
    if (!s.available())
    {
      delay(2);
      continue;
    }
    int c = s.peek();
    if ((c >= '0' && c <= '9') || c == '-' || c == '+')
    {
      return true;
    }
    s.read(); // discard
  }
  return false;
}

// Helper: reads a floating-point number, replacing ',' with '.'
static float readFloatFromStream(Stream &s, unsigned long timeoutMs = 1500)
{
  if (!skipToNumber(s, timeoutMs)) return NAN;

  char buf[16];
  int idx = 0;
  bool hasDot = false;
  unsigned long start = millis();

  while (idx < (int)sizeof(buf) - 1 && (millis() - start < timeoutMs))
  {
    if (!s.available())
    {
      delay(2);
      continue;
    }
    int c = s.peek();
    if ((c >= '0' && c <= '9') || c == '-' || c == '+')
    {
      buf[idx++] = (char)s.read();
    }
    else if ((c == '.' || c == ',') && !hasDot)
    {
      buf[idx++] = '.';
      hasDot = true;
      s.read();
    }
    else
    {
      break;
    }
  }
  buf[idx] = '\0';
  return (idx > 0) ? (float)atof(buf) : NAN;
}

// Helper: reads an integer from stream
static int readIntFromStream(Stream &s, unsigned long timeoutMs = 1500)
{
  float val = readFloatFromStream(s, timeoutMs);
  return std::isnan(val) ? 0 : (int)std::round(val);
}

// Helper: reads a word (letters only) from stream, e.g. "SW", "ESE", "NNE"
static bool readWordFromStream(Stream &s, char *out, size_t outSize, unsigned long timeoutMs = 1500)
{
  if (!out || outSize == 0) return false;
  out[0] = '\0';

  unsigned long start = millis();
  // Skip non-letters
  while (millis() - start < timeoutMs)
  {
    if (!s.available())
    {
      delay(2);
      continue;
    }
    int c = s.peek();
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
    {
      break;
    }
    s.read();
  }

  size_t idx = 0;
  while (idx < outSize - 1 && (millis() - start < timeoutMs))
  {
    if (!s.available())
    {
      delay(2);
      continue;
    }
    int c = s.peek();
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
    {
      out[idx++] = (char)s.read();
    }
    else
    {
      break;
    }
  }
  out[idx] = '\0';
  return idx > 0;
}

// Scrape Campania Live
bool fetchCampaniaLive(WiFiClient &client, local_station_t &hill)
{
  Serial.println("[Scraper] Connecting to Campania Live (Torre del Greco 154m)...");
  client.setTimeout(8000);

  // Resolve hostname with fallback IP to bypass local router DNS issues
  IPAddress serverIP;
  if (!WiFi.hostByName("www.campanialive.it", serverIP))
  {
    Serial.println("[Scraper] DNS fallback for www.campanialive.it -> 31.11.35.175");
    serverIP.fromString("31.11.35.175");
  }
  else
  {
    Serial.printf("[Scraper] Resolved www.campanialive.it -> %s\n", serverIP.toString().c_str());
  }

  if (!client.connect(serverIP, 80))
  {
    Serial.println("[Scraper] Failed to connect to www.campanialive.it (port 80)");
    hill.valid = false;
    return false;
  }

  // Send HTTP GET request with standard browser headers
  client.print("GET /dati-meteo.asp?stazione=torredelgreco HTTP/1.1\r\n"
               "Host: www.campanialive.it\r\n"
               "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36\r\n"
               "Accept: text/html,application/xhtml+xml\r\n"
               "Connection: close\r\n\r\n");
  client.flush();

  unsigned long timeout = millis() + 8000;
  while (client.connected() && !client.available() && millis() < timeout)
  {
    delay(10);
  }

  if (!client.available())
  {
    Serial.println("[Scraper] Campania Live response timeout");
    client.stop();
    hill.valid = false;
    return false;
  }

  String statusLine = client.readStringUntil('\n');
  if (statusLine.indexOf("200") < 0)
  {
    Serial.println("[Scraper] Campania Live non-200 HTTP status: " + statusLine);
    client.stop();
    hill.valid = false;
    return false;
  }

  // Skip headers until empty line (\r\n\r\n)
  while (client.connected())
  {
    String header = client.readStringUntil('\n');
    if (header == "\r" || header.isEmpty()) break;
  }

  client.setTimeout(6000);

  hill.temp = NAN;
  hill.temp_min = NAN;
  hill.temp_max = NAN;
  hill.humidity = 0;
  hill.pressure = 0.0f;
  hill.wind_kts = 0.0f;
  hill.wind_gust_kts = 0.0f;
  hill.rain_today = 0.0f;
  strlcpy(hill.wind_dir, "--", sizeof(hill.wind_dir));

  // Helper lambda to skip <font color="..."> tag opening within the current table row
  auto skipToValue = [&client]() {
    if (client.findUntil((char *)"<font", (char *)"</tr>"))
    {
      client.findUntil((char *)">", (char *)"</tr>");
    }
  };

  // 1. Temperatura attuale
  if (client.find("Temperatura attuale"))
  {
    skipToValue();
    hill.temp = readFloatFromStream(client);
    Serial.printf("[Scraper] Campania Live Temp: %.1f °C\n", hill.temp);
  }

  // 2. Temperatura massima
  if (client.find("Temperatura massima"))
  {
    skipToValue();
    hill.temp_max = readFloatFromStream(client);
    Serial.printf("[Scraper] Campania Live Temp Max: %.1f °C\n", hill.temp_max);
  }

  // 3. Temperatura minima
  if (client.find("Temperatura minima"))
  {
    skipToValue();
    hill.temp_min = readFloatFromStream(client);
    Serial.printf("[Scraper] Campania Live Temp Min: %.1f °C\n", hill.temp_min);
  }

  // 4. Umidità
  if (client.find("Umidit"))
  {
    skipToValue();
    hill.humidity = readIntFromStream(client);
    Serial.printf("[Scraper] Campania Live Hum: %d %%\n", hill.humidity);
  }

  // 5. Pressione
  if (client.find("Pressione sul livello"))
  {
    skipToValue();
    hill.pressure = readFloatFromStream(client);
    Serial.printf("[Scraper] Campania Live Press: %.1f hPa\n", hill.pressure);
  }

  // 6. Vento medio
  if (client.find("Vento medio"))
  {
    skipToValue();
    hill.wind_kts = readFloatFromStream(client);
    if (client.find("direz."))
    {
      readWordFromStream(client, hill.wind_dir, sizeof(hill.wind_dir));
    }
    Serial.printf("[Scraper] Campania Live Wind: %.1f kts, dir: %s\n", hill.wind_kts, hill.wind_dir);
  }

  // 7. Raffica massima
  if (client.find("Raffica massima"))
  {
    skipToValue();
    hill.wind_gust_kts = readFloatFromStream(client);
    Serial.printf("[Scraper] Campania Live Gust: %.1f kts\n", hill.wind_gust_kts);
  }

  // 8. Pioggia oggi
  if (client.find("Pioggia oggi"))
  {
    skipToValue();
    hill.rain_today = readFloatFromStream(client);
    Serial.printf("[Scraper] Campania Live Rain: %.1f mm\n", hill.rain_today);
  }

  client.stop();

  hill.valid = !std::isnan(hill.temp);
  if (hill.valid)
  {
    time_t now = time(nullptr);
    tm tmInfo;
    localtime_r(&now, &tmInfo);
    snprintf(hill.last_update, sizeof(hill.last_update), "%02d:%02d", tmInfo.tm_hour, tmInfo.tm_min);
  }
  return hill.valid;
}

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
