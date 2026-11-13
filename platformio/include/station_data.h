/* Station data structures for esp32-weather-epd multi-station monitor.
 * Supports Campania Live (Collina), Lega Navale (Porto), Indoor AHT20,
 * Today's forecast, and historical logs.
 */

#ifndef __STATION_DATA_H__
#define __STATION_DATA_H__

#include <Arduino.h>
#include <time.h>

// Bump whenever the layout of weather_bundle_t changes so stale RTC memory is
// not reinterpreted with the new layout.
#define WEATHER_MAGIC_NUMBER 0x57455045 // "WEPE" (layout v2: obs_epoch)

// A station reading older than this is flagged as stale on the display.
#define STATION_STALE_SECONDS (2 * 3600)

typedef struct local_station {
  bool  valid;
  float temp;           // °C
  float temp_min;       // °C
  float temp_max;       // °C
  float feels_like;     // °C
  int   humidity;       // %
  float pressure;       // hPa
  float wind_kts;       // knots
  float wind_gust_kts;  // knots
  int   wind_deg;       // degrees (0-359)
  char  wind_dir[8];    // e.g. "SSE", "NW"
  float rain_today;     // mm
  float solar_wm2;      // W/m² (Lega Navale)
  int   uv_index;       // UV index (Lega Navale)
  char  last_update[24];// station's own observation time, "DD/MM HH:MM"
  time_t obs_epoch;     // station's own observation time (0 = unknown)
} local_station_t;

typedef struct indoor_sensor {
  bool  valid;
  float temp;           // °C
  float humidity;       // %
} indoor_sensor_t;

typedef struct today_forecast {
  bool    valid;
  int     icon_id;          // OWM weather icon ID or generic (800=clear, 500=rain, etc.)
  char    condition[32];    // "Sereno", "Pioggia", etc.
  float   temp_min;         // °C
  float   temp_max;         // °C
  float   pop;              // Probability of precipitation (0-100%)
  float   rain_mm;          // Expected rain volume (mm)
  float   wind_max_kts;     // Expected max wind
  int64_t sunrise;          // Unix epoch UTC
  int64_t sunset;           // Unix epoch UTC
} today_forecast_t;

typedef struct daily_summary {
  char  date_str[12];       // "DD/MM"
  char  day_name[8];        // "Lun", "Mar", etc.
  float temp_min;           // °C
  float temp_max;           // °C
  float rain_total;         // mm
  float wind_max_kts;       // knots
  bool  has_data;
} daily_summary_t;

typedef struct hourly_sample {
  time_t timestamp;         // Unix epoch
  float  hill_temp;
  float  port_temp;
  float  indoor_temp;
  float  rain_mm;
} hourly_sample_t;

typedef struct weather_bundle {
  uint32_t         magic;             // WEATHER_MAGIC_NUMBER to verify RTC RAM validity
  local_station_t  hill;              // Campania Live (154m slm)
  local_station_t  port;              // Lega Navale (Pontile)
  indoor_sensor_t  indoor;            // On-board AHT20
  today_forecast_t today;             // Today's forecast & astro
  daily_summary_t  history[5];        // History of past 4 days + today
  hourly_sample_t  trend_24h[24];     // 24 samples for 24h temperature curve
  uint8_t          trend_count;       // Current count of valid trend points
  time_t           last_sync_epoch;   // Timestamp of last successful network fetch
  bool             is_offline;        // true if showing cached data due to WiFi failure
  uint8_t          current_page;      // 0: Dashboard, 1: History, 2: Nautical
  int              wifi_rssi;
  uint32_t         battery_mv;
} weather_bundle_t;

#endif // __STATION_DATA_H__
