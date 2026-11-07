/* Storage utility declarations for SD card logging and cache management.
 */

#ifndef __STORAGE_UTILS_H__
#define __STORAGE_UTILS_H__

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include "station_data.h"

// Initialize SD card on HSPI bus
bool initSD();
void powerOffSD();

// Cache management
bool saveLatestWeather(const weather_bundle_t &bundle);
bool loadLatestWeather(weather_bundle_t &bundle);

// Logging and historical analysis
bool appendLogCSV(time_t epoch, const weather_bundle_t &bundle);
bool updateHistoryAndTrend(weather_bundle_t &bundle, const tm *timeInfo);
void addTrendSample(weather_bundle_t &bundle, time_t ts);

#endif // __STORAGE_UTILS_H__
