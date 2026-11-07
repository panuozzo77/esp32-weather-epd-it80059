/* Web scrapers for Campania Live and Lega Navale Torre del Greco.
 */

#ifndef __SCRAPER_UTILS_H__
#define __SCRAPER_UTILS_H__

#include <Arduino.h>
#include <WiFiClient.h>
#include "station_data.h"

// Scrapes Campania Live weather station (154m slm, Torre del Greco)
bool fetchCampaniaLive(WiFiClient &client, local_station_t &hill);

// Scrapes / fetches Lega Navale weather station (Pontile, Torre del Greco)
bool fetchLegaNavale(WiFiClient &client, local_station_t &port);

// Parses today's forecast from OWM daily[0] / hourly or mock
void populateTodayForecastFromOWM(const void *owm_onecall_ptr, today_forecast_t &today);

#endif // __SCRAPER_UTILS_H__
