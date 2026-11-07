/* Web scraper implementation for Campania Live and Lega Navale Torre del Greco.
 */

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "scraper_utils.h"
#include "api_response.h"
#include "conversions.h"

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

// Scrape Lega Navale Torre del Greco
bool fetchLegaNavale(WiFiClient &client, local_station_t &port)
{
  Serial.println("[Scraper] Connecting to Lega Navale (Torre del Greco Pontile)...");

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  // Attempt standard HTTP on port 80
  http.begin(client, "www.leganavaletorredelgreco.it", 80, "/meteo/");
  int httpCode = http.GET();

  bool isHttpsFallback = false;
  WiFiClientSecure secClient;
  HTTPClient https;

  // Fallback to HTTPS on port 443 if port 80 times out or is rejected
  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("[Scraper] Port 80 status: %d (%s). Falling back to HTTPS...\n",
                  httpCode, http.errorToString(httpCode).c_str());
    http.end();

    secClient.setInsecure();
    secClient.setTimeout(15000);
    https.setConnectTimeout(8000);
    https.setTimeout(15000);
    https.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36");
    https.begin(secClient, "https://www.leganavaletorredelgreco.it/meteo/");
    httpCode = https.GET();
    isHttpsFallback = true;
  }

  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("[Scraper] Lega Navale failed with code %d\n", httpCode);
    if (isHttpsFallback) https.end();
    else http.end();
    port.valid = false;
    return false;
  }

  Serial.println("[Scraper] Lega Navale connected (HTTP 200). Parsing stream...");
  Stream &s = isHttpsFallback ? https.getStream() : http.getStream();
  s.setTimeout(15000);

  port.temp = NAN;
  port.temp_min = NAN;
  port.temp_max = NAN;
  port.humidity = 0;
  port.pressure = 0.0f;
  port.wind_kts = 0.0f;
  port.wind_gust_kts = 0.0f;
  port.wind_deg = 0;
  port.solar_wm2 = 0.0f;
  port.uv_index = 0;
  port.rain_today = 0.0f;
  strlcpy(port.wind_dir, "--", sizeof(port.wind_dir));

  // 1. Wind speed from Ecowitt strip
  if (s.find("data-rt=\"wind\">"))
  {
    port.wind_kts = readFloatFromStream(s);
    Serial.printf("[Scraper] Lega Navale Wind: %.1f kts\n", port.wind_kts);
  }

  // 2. Wind gust
  if (s.find("data-rt=\"gust\">"))
  {
    port.wind_gust_kts = readFloatFromStream(s);
    Serial.printf("[Scraper] Lega Navale Gust: %.1f kts\n", port.wind_gust_kts);
  }

  // 3. Temperature at pontile
  if (s.find("data-rt=\"temp\">"))
  {
    port.temp = readFloatFromStream(s);
    Serial.printf("[Scraper] Lega Navale Temp: %.1f °C\n", port.temp);
  }

  // 4. Pressure
  if (s.find("data-rt=\"press\">"))
  {
    port.pressure = readFloatFromStream(s);
    Serial.printf("[Scraper] Lega Navale Press: %.1f hPa\n", port.pressure);
  }

  // 5. Rain rate (skip instant rate)
  if (s.find("data-rt=\"rain\">"))
  {
    readFloatFromStream(s);
  }

  // 6. Wind degrees
  if (s.find("data-rt=\"dir\">"))
  {
    port.wind_deg = readIntFromStream(s);
    Serial.printf("[Scraper] Lega Navale Wind Deg: %d°\n", port.wind_deg);
  }

  // 7. Wind direction label
  if (s.find("data-rt=\"dir_label\">"))
  {
    readWordFromStream(s, port.wind_dir, sizeof(port.wind_dir));
    Serial.printf("[Scraper] Lega Navale Wind Dir: %s\n", port.wind_dir);
  }

  // 8. Humidity al pontile
  if (s.find("Umidit"))
  {
    if (s.find("ecowitt-value\">"))
    {
      port.humidity = readIntFromStream(s);
      Serial.printf("[Scraper] Lega Navale Hum: %d %%\n", port.humidity);
    }
  }

  // 9. Solar irradiance
  if (s.find("Irradianza"))
  {
    if (s.find("ecowitt-value\">"))
    {
      port.solar_wm2 = readFloatFromStream(s);
      Serial.printf("[Scraper] Lega Navale Solar: %.1f W/m2\n", port.solar_wm2);
    }
  }

  // 10. UV Index
  if (s.find("Indice UV"))
  {
    if (s.find("ecowitt-value\">"))
    {
      port.uv_index = readIntFromStream(s);
      Serial.printf("[Scraper] Lega Navale UV: %d\n", port.uv_index);
    }
  }

  // 11. Rain today
  if (s.find("Pioggia"))
  {
    if (s.find("Oggi"))
    {
      if (s.find("ecowitt-value\">"))
      {
        port.rain_today = readFloatFromStream(s);
        Serial.printf("[Scraper] Lega Navale Rain: %.1f mm\n", port.rain_today);
      }
    }
  }

  if (isHttpsFallback) https.end();
  else http.end();

  port.valid = !std::isnan(port.temp);
  if (port.valid)
  {
    time_t now = time(nullptr);
    tm tmInfo;
    localtime_r(&now, &tmInfo);
    snprintf(port.last_update, sizeof(port.last_update), "%02d:%02d", tmInfo.tm_hour, tmInfo.tm_min);
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
