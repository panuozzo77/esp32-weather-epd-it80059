/* Renderer for esp32-weather-epd.
 * Copyright (C) 2022-2026  Luke Marzen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <cmath>
#include "_locale.h"
#include "_strftime.h"
#include "renderer.h"
#include "api_response.h"
#include "config.h"
#include "conversions.h"
#include "display_utils.h"

// fonts
#include FONT_HEADER

// icon header files
#include "icons/icons_16x16.h"
#include "icons/icons_24x24.h"
#include "icons/icons_32x32.h"
#include "icons/icons_48x48.h"
#include "icons/icons_64x64.h"
#include "icons/icons_96x96.h"
#include "icons/icons_128x128.h"
#include "icons/icons_160x160.h"
#include "icons/icons_196x196.h"

#ifdef DISP_BW_V2
  GxEPD2_BW<GxEPD2_750_GDEY075T7,
            GxEPD2_750_GDEY075T7::HEIGHT> display(
    GxEPD2_750_GDEY075T7(PIN_EPD_CS,
                         PIN_EPD_DC,
                         PIN_EPD_RST,
                         PIN_EPD_BUSY));
#endif
#ifdef DISP_3C_B
  GxEPD2_3C<GxEPD2_750c_GDEY075Z08,
            GxEPD2_750c_GDEY075Z08::HEIGHT / 2> display(
    GxEPD2_750c_GDEY075Z08(PIN_EPD_CS,
                           PIN_EPD_DC,
                           PIN_EPD_RST,
                           PIN_EPD_BUSY));
#endif
#ifdef DISP_7C_F
  GxEPD2_7C<GxEPD2_730c_GDEY073D46,
            GxEPD2_730c_GDEY073D46::HEIGHT / 4> display(
    GxEPD2_730c_GDEY073D46(PIN_EPD_CS,
                           PIN_EPD_DC,
                           PIN_EPD_RST,
                           PIN_EPD_BUSY));
#endif
#ifdef DISP_BW_V1
  GxEPD2_BW<GxEPD2_750,
            GxEPD2_750::HEIGHT> display(
    GxEPD2_750(PIN_EPD_CS,
               PIN_EPD_DC,
               PIN_EPD_RST,
               PIN_EPD_BUSY));
#endif
#ifdef DISP_3C_E420
  GxEPD2_3C<GxEPD2_420c_GDEY042Z98,
            GxEPD2_420c_GDEY042Z98::HEIGHT / 2> display(
    GxEPD2_420c_GDEY042Z98(PIN_EPD_CS,
                           PIN_EPD_DC,
                           PIN_EPD_RST,
                           PIN_EPD_BUSY));
#endif

#ifndef ACCENT_COLOR
  #define ACCENT_COLOR GxEPD_BLACK
#endif

// Widget layout constants — different for 4.2in (400x300) vs 7.5in (800x480)
// Layout grid for current-condition widgets:
//   DISP_3C_E420 (4.2in): 2 cols × 5 rows, icon=24×24, col=75px, base_y=104
//   others       (7.5in): 2 cols × 5 rows, icon=48×48, col=162px, base_y=204
#ifdef DISP_3C_E420
  static const int WI_COL  = 84;   // column width (px) — keeps right column
                                   // clear of the outlook graph's y-axis labels
  static const int WI_Y0   = 104;  // first-row base y
  static const int WI_DY   = 30;   // row stride (icon 24 + gap 6)
  static const int WI_SZ   = 24;   // icon size
  static const int WI_LOFF = 24;   // label/data x offset from col start
  static const int WI_LDY  = 5;    // label baseline delta from row base
  static const int WI_DDY  = 17;   // data baseline delta from row base (5 + 24/2)
#else
  static const int WI_COL  = 162;  // column width (px)
  static const int WI_Y0   = 204;  // first-row base y
  static const int WI_DY   = 56;   // row stride (icon 48 + gap 8)
  static const int WI_SZ   = 48;   // icon size
  static const int WI_LOFF = 48;   // label/data x offset from col start
  static const int WI_LDY  = 10;   // label baseline delta from row base
  static const int WI_DDY  = 32;   // data baseline delta from row base (8 + 48/2)
#endif

/* Returns the string width in pixels
 */
uint16_t getStringWidth(const String &text)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;
}

/* Returns the string height in pixels
 */
uint16_t getStringHeight(const String &text)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return h;
}

/* Draws a string with alignment
 */
void drawString(int16_t x, int16_t y, const String &text, alignment_t alignment,
                uint16_t color)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextColor(color);
  display.getTextBounds(text, x, y, &x1, &y1, &w, &h);
  if (alignment == RIGHT)
  {
    x = x - w;
  }
  if (alignment == CENTER)
  {
    x = x - w / 2;
  }
  display.setCursor(x, y);
  display.print(text);
  return;
} // end drawString

/* Draws a string that will flow into the next line when max_width is reached.
 * If a string exceeds max_lines an ellipsis (...) will terminate the last word.
 * Lines will break at spaces(' ') and dashes('-').
 *
 * Note: max_width should be big enough to accommodate the largest word that
 *       will be displayed. If an unbroken string of characters longer than
 *       max_width exist in text, then the string will be printed beyond
 *       max_width.
 */
void drawMultiLnString(int16_t x, int16_t y, const String &text,
                       alignment_t alignment, uint16_t max_width,
                       uint16_t max_lines, int16_t line_spacing,
                       uint16_t color)
{
  uint16_t current_line = 0;
  String textRemaining = text;
  // print until we reach max_lines or no more text remains
  while (current_line < max_lines && !textRemaining.isEmpty())
  {
    int16_t  x1, y1;
    uint16_t w, h;

    display.getTextBounds(textRemaining, 0, 0, &x1, &y1, &w, &h);

    int endIndex = textRemaining.length();
    // check if remaining text is to wide, if it is then print what we can
    String subStr = textRemaining;
    int splitAt = 0;
    int keepLastChar = 0;
    while (w > max_width && splitAt != -1)
    {
      if (keepLastChar)
      {
        // if we kept the last character during the last iteration of this while
        // loop, remove it now so we don't get stuck in an infinite loop.
        subStr.remove(subStr.length() - 1);
      }

      // find the last place in the string that we can break it.
      if (current_line < max_lines - 1)
      {
        splitAt = std::max(subStr.lastIndexOf(" "),
                           subStr.lastIndexOf("-"));
      }
      else
      {
        // this is the last line, only break at spaces so we can add ellipsis
        splitAt = subStr.lastIndexOf(" ");
      }

      // if splitAt == -1 then there is an unbroken set of characters that is
      // longer than max_width. Otherwise if splitAt != -1 then we can continue
      // the loop until the string is <= max_width
      if (splitAt != -1)
      {
        endIndex = splitAt;
        subStr = subStr.substring(0, endIndex + 1);

        char lastChar = subStr.charAt(endIndex);
        if (lastChar == ' ')
        {
          // remove this char now so it is not counted towards line width
          keepLastChar = 0;
          subStr.remove(endIndex);
          --endIndex;
        }
        else if (lastChar == '-')
        {
          // this char will be printed on this line and removed next iteration
          keepLastChar = 1;
        }

        if (current_line < max_lines - 1)
        {
          // this is not the last line
          display.getTextBounds(subStr, 0, 0, &x1, &y1, &w, &h);
        }
        else
        {
          // this is the last line, we need to make sure there is space for
          // ellipsis
          display.getTextBounds(subStr + "...", 0, 0, &x1, &y1, &w, &h);
          if (w <= max_width)
          {
            // ellipsis fit, add them to subStr
            subStr = subStr + "...";
          }
        }

      } // end if (splitAt != -1)
    } // end inner while

    drawString(x, y + (current_line * line_spacing), subStr, alignment, color);

    // update textRemaining to no longer include what was printed
    // +1 for exclusive bounds, +1 to get passed space/dash
    textRemaining = textRemaining.substring(endIndex + 2 - keepLastChar);

    ++current_line;
  } // end outer while

  return;
} // end drawMultiLnString

/* Initialize e-paper display
 */
void initDisplay()
{
  pinMode(PIN_EPD_PWR, OUTPUT);
  digitalWrite(PIN_EPD_PWR, HIGH);
#ifdef DRIVER_WAVESHARE
  display.init(115200, true, 2, false);
#endif
#ifdef DRIVER_DESPI_C02
  display.init(115200, true, 10, false);
#endif
  // remap spi
  SPI.end();
  SPI.begin(PIN_EPD_SCK,
            PIN_EPD_MISO,
            PIN_EPD_MOSI,
            PIN_EPD_CS);

  display.setRotation(0);
  display.setTextSize(1);
  display.setTextColor(GxEPD_BLACK);
  display.setTextWrap(false);
  // display.fillScreen(GxEPD_WHITE);
  display.setFullWindow();
  display.firstPage(); // use paged drawing mode, sets fillScreen(GxEPD_WHITE)
  return;
} // end initDisplay

/* Power-off e-paper display
 */
void powerOffDisplay()
{
  display.hibernate(); // turns powerOff() and sets controller to deep sleep for
                       // minimum power use
  digitalWrite(PIN_EPD_PWR, LOW);
  return;
} // end initDisplay


/* These functions are responsible for drawing the current conditions and
 * associated icons on the left panel.
 */

// drawCurrentSunrise
#ifdef POS_SUNRISE
void drawCurrentSunrise(const owm_current_t &current)
{
  String dataStr, unitStr;
  int PosX = POS_SUNRISE % 2;
  int PosY = static_cast<int>(POS_SUNRISE / 2);
    // icons
  display.drawInvertedBitmap(WI_COL * PosX, WI_Y0 + WI_DY * PosY,
#ifdef DISP_3C_E420
                             wi_sunrise_24x24, 24, 24, GxEPD_BLACK);
#else
                             wi_sunrise_48x48, 48, 48, GxEPD_BLACK);
#endif

  // labels
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_LDY + WI_DY * PosY, TXT_SUNRISE, LEFT);

  // sunrise
#ifdef DISP_3C_E420
  display.setFont(&FONT_7pt8b);
#else
  display.setFont(&FONT_12pt8b);
#endif
  char timeBuffer[12] = {}; // big enough to accommodate "hh:mm:ss am"
  time_t ts = current.sunrise;
  tm *timeInfo = localtime(&ts);
  _strftime(timeBuffer, sizeof(timeBuffer), TIME_FORMAT, timeInfo);
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_DDY + WI_DY * PosY, timeBuffer, LEFT, ACCENT_COLOR);

  return;
}
# endif
// end drawCurrentSunrise

// drawCurrentWind
#ifdef POS_WIND
void drawCurrentWind(const owm_current_t &current)
{
  String dataStr, unitStr;
  int PosX = (POS_WIND % 2);
  int PosY = static_cast<int>(POS_WIND / 2);

  // icons
  display.drawInvertedBitmap(WI_COL * PosX, WI_Y0 + WI_DY * PosY,
#ifdef DISP_3C_E420
                             wi_strong_wind_24x24, 24, 24, GxEPD_BLACK);
#else
                             wi_strong_wind_48x48, 48, 48, GxEPD_BLACK);
#endif

  // labels
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_LDY + WI_DY * PosY, TXT_WIND, LEFT);

  // wind
#ifdef DISP_3C_E420
  display.setFont(&FONT_7pt8b);
#else
  display.setFont(&FONT_12pt8b);
#endif
#if defined(WIND_INDICATOR_ARROW) && !defined(DISP_3C_E420)
  display.drawInvertedBitmap(WI_LOFF + (WI_COL * PosX), WI_Y0 + 24 / 2 + WI_DY * PosY,
                             getWindBitmap24(current.wind_deg),
                             24, 24, GxEPD_BLACK);
#endif
#ifdef UNITS_SPEED_METERSPERSECOND
  dataStr = String(static_cast<int>(std::round(current.wind_speed)));
  unitStr = String(" ") + TXT_UNITS_SPEED_METERSPERSECOND;
#endif
#ifdef UNITS_SPEED_FEETPERSECOND
  dataStr = String(static_cast<int>(std::round(
                   meterspersecond_to_feetpersecond(current.wind_speed) )));
  unitStr = String(" ") + TXT_UNITS_SPEED_FEETPERSECOND;
#endif
#ifdef UNITS_SPEED_KILOMETERSPERHOUR
  dataStr = String(static_cast<int>(std::round(
                   meterspersecond_to_kilometersperhour(current.wind_speed) )));
  unitStr = String(" ") + TXT_UNITS_SPEED_KILOMETERSPERHOUR;
#endif
#ifdef UNITS_SPEED_MILESPERHOUR
  dataStr = String(static_cast<int>(std::round(
                   meterspersecond_to_milesperhour(current.wind_speed) )));
  unitStr = String(" ") + TXT_UNITS_SPEED_MILESPERHOUR;
#endif
#ifdef UNITS_SPEED_KNOTS
  dataStr = String(static_cast<int>(std::round(
                   meterspersecond_to_knots(current.wind_speed) )));
  unitStr = String(" ") + TXT_UNITS_SPEED_KNOTS;
#endif
#ifdef UNITS_SPEED_BEAUFORT
  dataStr = String(meterspersecond_to_beaufort(current.wind_speed));
  unitStr = String(" ") + TXT_UNITS_SPEED_BEAUFORT;
#endif

#if defined(WIND_INDICATOR_ARROW) && !defined(DISP_3C_E420)
  drawString((WI_LOFF + 24) + (WI_COL * PosX), WI_Y0 + WI_DDY + WI_DY * PosY, dataStr, LEFT, ACCENT_COLOR);
#else
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_DDY + WI_DY * PosY, dataStr, LEFT, ACCENT_COLOR);
#endif
#ifdef DISP_3C_E420
  display.setFont(&FONT_5pt8b);
#else
  display.setFont(&FONT_8pt8b);
#endif
  drawString(display.getCursorX(), WI_Y0 + WI_DDY + WI_DY * PosY, unitStr, LEFT);

#if defined(WIND_INDICATOR_NUMBER)
  dataStr = String(current.wind_deg) + "\260";
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
  drawString(display.getCursorX() + 3, WI_Y0 + WI_DDY + WI_DY * PosY, dataStr, LEFT, ACCENT_COLOR);
#else
  display.setFont(&FONT_12pt8b);
  drawString(display.getCursorX() + 6, WI_Y0 + WI_DDY + WI_DY * PosY, dataStr, LEFT, ACCENT_COLOR);
#endif
#endif
#if defined(WIND_INDICATOR_CPN_CARDINAL)                \
 || defined(WIND_INDICATOR_CPN_INTERCARDINAL)           \
 || defined(WIND_INDICATOR_CPN_SECONDARY_INTERCARDINAL) \
 || defined(WIND_INDICATOR_CPN_TERTIARY_INTERCARDINAL)
  dataStr = getCompassPointNotation(current.wind_deg);
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
  drawString(display.getCursorX() + 3, WI_Y0 + WI_DDY + WI_DY * PosY, dataStr, LEFT, ACCENT_COLOR);
#else
  display.setFont(&FONT_12pt8b);
  drawString(display.getCursorX() + 6, WI_Y0 + WI_DDY + WI_DY * PosY, dataStr, LEFT, ACCENT_COLOR);
#endif
#endif

  return;
}
#endif
// end drawCurrentWind

// drawCurrentUVI
#ifdef POS_UVI
void drawCurrentUVI(const owm_current_t &current)
{
  String dataStr, unitStr;
  int PosX = (POS_UVI % 2);
  int PosY = static_cast<int>(POS_UVI / 2);

  // icons
  display.drawInvertedBitmap(WI_COL * PosX, WI_Y0 + WI_DY * PosY,
#ifdef DISP_3C_E420
                             wi_day_sunny_24x24, 24, 24, GxEPD_BLACK);
#else
                             wi_day_sunny_48x48, 48, 48, GxEPD_BLACK);
#endif

  // labels
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  const int labelX = WI_LOFF + (WI_COL * PosX);
#ifdef DISP_3C_E420
  drawString(labelX, WI_Y0 + WI_LDY + WI_DY * PosY, "UV", LEFT);
#else
  drawString(labelX, WI_Y0 + WI_LDY + WI_DY * PosY, TXT_UV_INDEX, LEFT);
#endif

  // spacing between end of index value and start of descriptor text
  #ifdef DISP_3C_E420
    const int sp = 4;
  #else
    const int sp = 8;
  #endif

  // uv index
#ifdef DISP_3C_E420
  display.setFont(&FONT_7pt8b);
#else
  display.setFont(&FONT_12pt8b);
#endif
  unsigned int uvi = static_cast<unsigned int>(
                                std::max(std::round(current.uvi), 0.0f));
  dataStr = String(uvi);
  drawString(labelX, WI_Y0 + WI_DDY + WI_DY * PosY, dataStr, LEFT, ACCENT_COLOR);
#ifdef DISP_3C_E420
  display.setFont(&FONT_5pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  dataStr = String(getUVIdesc(uvi));
#ifdef DISP_3C_E420
  const int max_w = (WI_COL + (PosX * WI_COL) - sp) - (display.getCursorX() + sp) - 4;
  const int descY = WI_Y0 + WI_DDY + WI_DY * PosY + 4;
#else
  int max_w = (WI_COL + (PosX * WI_COL) - sp) - (display.getCursorX() + sp);
  const int descY = WI_Y0 + WI_DDY + WI_DY * PosY;
#endif
  if (getStringWidth(dataStr) <= max_w)
  { // Fits on a single line, draw along bottom
    drawString(display.getCursorX() + sp, descY, dataStr, LEFT, ACCENT_COLOR);
  }
  else
  { // use smaller font
    display.setFont(&FONT_5pt8b);
    if (getStringWidth(dataStr) <= max_w)
    { // Fits on a single line with smaller font, draw along bottom
      drawString(display.getCursorX() + sp, descY, dataStr, LEFT,
                 ACCENT_COLOR);
    }
    else
    { // Does not fit on a single line, draw higher to allow room for 2nd line
      drawMultiLnString(display.getCursorX() + sp,
#ifdef DISP_3C_E420
                        WI_Y0 + WI_DDY + WI_DY * PosY - 8,
                        dataStr, LEFT, max_w, 2, 9, ACCENT_COLOR);
#else
                        WI_Y0 + WI_DDY + WI_DY * PosY - 10,
                        dataStr, LEFT, max_w, 2, 10, ACCENT_COLOR);
#endif
    }
  }
  return;
}
#endif
// end drawCurrentUVI

// drawCurrentAirQuality
#ifdef POS_AIR_QULITY
void drawCurrentAirQuality(const owm_resp_air_pollution_t &owm_air_pollution)
{
  String dataStr, unitStr;
  int PosX = (POS_AIR_QULITY % 2);
  int PosY = static_cast<int>(POS_AIR_QULITY / 2);

  // icons
  display.drawInvertedBitmap(WI_COL * PosX, WI_Y0 + WI_DY * PosY,
#ifdef DISP_3C_E420
                             air_filter_24x24, 24, 24, GxEPD_BLACK);
#else
                             air_filter_48x48, 48, 48, GxEPD_BLACK);
#endif

  // labels
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif

  const int labelX = WI_LOFF + (WI_COL * PosX);
#ifdef DISP_3C_E420
  drawString(labelX, WI_Y0 + WI_LDY + WI_DY * PosY, "AQI", LEFT);
#else
  const char *air_quality_index_label;
  if (aqi_desc_type(AQI_SCALE) == AIR_QUALITY_DESC)
  {
    air_quality_index_label = TXT_AIR_QUALITY;
  }
  else // (aqi_desc_type(AQI_SCALE) == AIR_POLLUTION_DESC)
  {
    air_quality_index_label = TXT_AIR_POLLUTION;
  }
  drawString(labelX, WI_Y0 + WI_LDY + WI_DY * PosY, air_quality_index_label, LEFT);
#endif

  // spacing between end of index value and start of descriptor text
  #ifdef DISP_3C_E420
    const int sp = 4;
  #else
    const int sp = 8;
  #endif

  // air quality index
#ifdef DISP_3C_E420
  display.setFont(&FONT_7pt8b);
#else
  display.setFont(&FONT_12pt8b);
#endif
  const owm_components_t &c = owm_air_pollution.components;
  // OpenWeatherMap does not provide pb (lead) conentrations, so we pass NULL.
  int aqi = calc_aqi(AQI_SCALE, c.co, c.nh3, c.no, c.no2, c.o3, NULL, c.so2,
                                c.pm10, c.pm2_5);
  int aqi_max = aqi_scale_max(AQI_SCALE);
  if (aqi > aqi_max)
  {
    dataStr = "> " + String(aqi_max);
  }
  else
  {
    dataStr = String(aqi);
  }
  drawString(labelX, WI_Y0 + WI_DDY + WI_DY * PosY, dataStr, LEFT, ACCENT_COLOR);
#ifdef DISP_3C_E420
  display.setFont(&FONT_5pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  dataStr = String(aqi_desc(AQI_SCALE, aqi));
#ifdef DISP_3C_E420
  const int max_w = (WI_COL + (PosX * WI_COL) - sp) - (display.getCursorX() + sp) - 4;
#else
  int max_w = (WI_COL + (PosX * WI_COL) - sp) - (display.getCursorX() + sp);
#endif
  if (getStringWidth(dataStr) <= max_w)
  { // Fits on a single line, draw along bottom
    drawString(display.getCursorX() + sp, WI_Y0 + WI_DDY + WI_DY * PosY,
               dataStr, LEFT, ACCENT_COLOR);
  }
  else
  { // use smaller font
    display.setFont(&FONT_5pt8b);
    if (getStringWidth(dataStr) <= max_w)
    { // Fits on a single line with smaller font, draw along bottom
      drawString(display.getCursorX() + sp,
                 WI_Y0 + WI_DDY + WI_DY * PosY,
                 dataStr, LEFT, ACCENT_COLOR);
    }
    else
    { // Does not fit on a single line, draw higher to allow room for 2nd line
      drawMultiLnString(display.getCursorX() + sp,
#ifdef DISP_3C_E420
                        WI_Y0 + WI_DDY + WI_DY * PosY - 8,
                        dataStr, LEFT, max_w, 2, 9, ACCENT_COLOR);
#else
                        WI_Y0 + WI_DDY + WI_DY * PosY - 10,
                        dataStr, LEFT, max_w, 2, 10, ACCENT_COLOR);
#endif
    }
  }

  return;
}
#endif
// end drawCurrentAirQuality

// drawCurrentInTemp
#ifdef POS_INTEMP
void drawCurrentInTemp(float inTemp)
{
  String dataStr, unitStr;
  int PosX = (POS_INTEMP % 2);
  int PosY = static_cast<int>(POS_INTEMP / 2);

  // icons
  display.drawInvertedBitmap(WI_COL * PosX, WI_Y0 + WI_DY * PosY,
#ifdef DISP_3C_E420
                             house_thermometer_24x24, 24, 24, GxEPD_BLACK);
#else
                             house_thermometer_48x48, 48, 48, GxEPD_BLACK);
#endif

  // labels
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_LDY + WI_DY * PosY, TXT_INDOOR_TEMPERATURE, LEFT);

  // indoor temperature
#ifdef DISP_3C_E420
  display.setFont(&FONT_7pt8b);
#else
  display.setFont(&FONT_12pt8b);
#endif
  if (!std::isnan(inTemp))
  {
#ifdef UNITS_TEMP_KELVIN
    dataStr = String(std::round(celsius_to_kelvin(inTemp) * 10) / 10.0f, 1);
#endif
#ifdef UNITS_TEMP_CELSIUS
    dataStr = String(std::round(inTemp * 10) / 10.0f, 1);
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
    dataStr = String(static_cast<int>(
              std::round(celsius_to_fahrenheit(inTemp))));
#endif
  }
  else
  {
    dataStr = "--";
  }
#if defined(UNITS_TEMP_CELSIUS) || defined(UNITS_TEMP_FAHRENHEIT)
  dataStr += "\260";
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_DDY + WI_DY * PosY, dataStr, LEFT, ACCENT_COLOR);
  return;
}
#endif
// end drawCurrentInTemp

// drawCurrentSunset
#ifdef POS_SUNSET
void drawCurrentSunset(const owm_current_t &current)
{
  String dataStr, unitStr;
  int PosX = (POS_SUNSET % 2);
  int PosY = static_cast<int>(POS_SUNSET / 2);
  // icons
  display.drawInvertedBitmap(WI_COL * PosX, WI_Y0 + WI_DY * PosY,
#ifdef DISP_3C_E420
                             wi_sunset_24x24, 24, 24, GxEPD_BLACK);
#else
                             wi_sunset_48x48, 48, 48, GxEPD_BLACK);
#endif

  // labels
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_LDY + WI_DY * PosY, TXT_SUNSET, LEFT);

  // sunset
#ifdef DISP_3C_E420
  display.setFont(&FONT_7pt8b);
#else
  display.setFont(&FONT_12pt8b);
#endif
  char timeBuffer[12] = {}; // big enough to accommodate "hh:mm:ss am"
  time_t ts = current.sunset;
  tm *timeInfo = localtime(&ts);
  _strftime(timeBuffer, sizeof(timeBuffer), TIME_FORMAT, timeInfo);
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_DDY + WI_DY * PosY, timeBuffer, LEFT, ACCENT_COLOR);

  return;
}
#endif
// end drawCurrentSunset

// drawCurrentHumidity
#ifdef POS_HUMIDITY
void drawCurrentHumidity(const owm_current_t &current)
{
  String dataStr, unitStr;
  int PosX = (POS_HUMIDITY % 2);
  int PosY = static_cast<int>(POS_HUMIDITY / 2);

  // icons
  display.drawInvertedBitmap(WI_COL * PosX, WI_Y0 + WI_DY * PosY,
#ifdef DISP_3C_E420
                             wi_humidity_24x24, 24, 24, GxEPD_BLACK);
#else
                             wi_humidity_48x48, 48, 48, GxEPD_BLACK);
#endif

  // labels
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_LDY + WI_DY * PosY, TXT_HUMIDITY, LEFT);

  // humidity
#ifdef DISP_3C_E420
  display.setFont(&FONT_7pt8b);
#else
  display.setFont(&FONT_12pt8b);
#endif
  dataStr = String(current.humidity);
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_DDY + WI_DY * PosY, dataStr, LEFT, ACCENT_COLOR);
#ifdef DISP_3C_E420
  display.setFont(&FONT_5pt8b);
#else
  display.setFont(&FONT_8pt8b);
#endif
  drawString(display.getCursorX(), WI_Y0 + WI_DDY + WI_DY * PosY, "%", LEFT, ACCENT_COLOR);
  return;
}
#endif
// end drawCurrentHumidity

// drawCurrentPressure
#ifdef POS_PRESSURE
void drawCurrentPressure(const owm_current_t &current)
{
  String dataStr, unitStr;
  int PosX = (POS_PRESSURE % 2);
  int PosY = static_cast<int>(POS_PRESSURE / 2);
  //  icons
  display.drawInvertedBitmap(WI_COL * PosX, WI_Y0 + WI_DY * PosY,
#ifdef DISP_3C_E420
                             wi_barometer_24x24, 24, 24, GxEPD_BLACK);
#else
                             wi_barometer_48x48, 48, 48, GxEPD_BLACK);
#endif

  //  labels
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_LDY + WI_DY * PosY, TXT_PRESSURE, LEFT);

  // pressure
#ifdef UNITS_PRES_HECTOPASCALS
  dataStr = String(current.pressure);
  unitStr = String(" ") + TXT_UNITS_PRES_HECTOPASCALS;
#endif
#ifdef UNITS_PRES_PASCALS
  dataStr = String(static_cast<int>(std::round(
                   hectopascals_to_pascals(current.pressure) )));
  unitStr = String(" ") + TXT_UNITS_PRES_PASCALS;
#endif
#ifdef UNITS_PRES_MILLIMETERSOFMERCURY
  dataStr = String(static_cast<int>(std::round(
                   hectopascals_to_millimetersofmercury(current.pressure) )));
  unitStr = String(" ") + TXT_UNITS_PRES_MILLIMETERSOFMERCURY;
#endif
#ifdef UNITS_PRES_INCHESOFMERCURY
  dataStr = String(std::round(1e1f *
                   hectopascals_to_inchesofmercury(current.pressure)
                   ) / 1e1f, 1);
  unitStr = String(" ") + TXT_UNITS_PRES_INCHESOFMERCURY;
#endif
#ifdef UNITS_PRES_MILLIBARS
  dataStr = String(static_cast<int>(std::round(
                   hectopascals_to_millibars(current.pressure) )));
  unitStr = String(" ") + TXT_UNITS_PRES_MILLIBARS;
#endif
#ifdef UNITS_PRES_ATMOSPHERES
  dataStr = String(std::round(1e3f *
                   hectopascals_to_atmospheres(current.pressure) )
                   / 1e3f, 3);
  unitStr = String(" ") + TXT_UNITS_PRES_ATMOSPHERES;
#endif
#ifdef UNITS_PRES_GRAMSPERSQUARECENTIMETER
  dataStr = String(static_cast<int>(std::round(
                   hectopascals_to_gramspersquarecentimeter(current.pressure)
                   )));
  unitStr = String(" ") + TXT_UNITS_PRES_GRAMSPERSQUARECENTIMETER;
#endif
#ifdef UNITS_PRES_POUNDSPERSQUAREINCH
  dataStr = String(std::round(1e2f *
                   hectopascals_to_poundspersquareinch(current.pressure)
                   ) / 1e2f, 2);
  unitStr = String(" ") + TXT_UNITS_PRES_POUNDSPERSQUAREINCH;
#endif
#ifdef DISP_3C_E420
  display.setFont(&FONT_7pt8b);
#else
  display.setFont(&FONT_12pt8b);
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_DDY + WI_DY * PosY, dataStr, LEFT, ACCENT_COLOR);
#ifdef DISP_3C_E420
  display.setFont(&FONT_5pt8b);
#else
  display.setFont(&FONT_8pt8b);
#endif
  drawString(display.getCursorX(), WI_Y0 + WI_DDY + WI_DY * PosY,
             unitStr, LEFT, ACCENT_COLOR);

  return;
}
#endif
// end drawCurrentPressure

// drawCurrentVisibility
#ifdef POS_VISIBILITY
void drawCurrentVisibility(const owm_current_t &current)
{
  String dataStr, unitStr;
  int PosX = (POS_VISIBILITY % 2);
  int PosY = static_cast<int>(POS_VISIBILITY / 2);

  // icons
  display.drawInvertedBitmap(WI_COL * PosX, WI_Y0 + WI_DY * PosY,
#ifdef DISP_3C_E420
                             visibility_icon_24x24, 24, 24, GxEPD_BLACK);
#else
                             visibility_icon_48x48, 48, 48, GxEPD_BLACK);
#endif

  // labels
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_LDY + WI_DY * PosY, TXT_VISIBILITY, LEFT);

  // visibility
#ifdef DISP_3C_E420
  display.setFont(&FONT_7pt8b);
#else
  display.setFont(&FONT_12pt8b);
#endif
#ifdef UNITS_DIST_KILOMETERS
  float vis = meters_to_kilometers(current.visibility);
  unitStr = String(" ") + TXT_UNITS_DIST_KILOMETERS;
#endif
#ifdef UNITS_DIST_MILES
  float vis = meters_to_miles(current.visibility);
  unitStr = String(" ") + TXT_UNITS_DIST_MILES;
#endif
  // if visibility is less than 1.95, round to 1 decimal place
  // else round to int
  if (vis < 1.95)
  {
    dataStr = String(std::round(10 * vis) / 10.0, 1);
  }
  else
  {
    dataStr = String(static_cast<int>(std::round(vis)));
  }
#ifdef UNITS_DIST_KILOMETERS
  if (vis >= 10)
  {
#endif
#ifdef UNITS_DIST_MILES
  if (vis >= 6)
  {
#endif
    dataStr = "> " + dataStr;
  }
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_DDY + WI_DY * PosY, dataStr, LEFT, ACCENT_COLOR);
#ifdef DISP_3C_E420
  display.setFont(&FONT_5pt8b);
#else
  display.setFont(&FONT_8pt8b);
#endif
  drawString(display.getCursorX(), WI_Y0 + WI_DDY + WI_DY * PosY,
             unitStr, LEFT, ACCENT_COLOR);

  return;
}
#endif
// end drawCurrentVisibility

// drawCurrentInHumidit
#ifdef POS_INHUMIDITY
void drawCurrentInHumidity(float inHumidity)
{
  String dataStr, unitStr;
  int PosX = (POS_INHUMIDITY % 2);
  int PosY = static_cast<int>(POS_INHUMIDITY / 2);

  // current weather data icons
  display.drawInvertedBitmap(WI_COL * PosX, WI_Y0 + WI_DY * PosY,
#ifdef DISP_3C_E420
                             house_humidity_24x24, 24, 24, GxEPD_BLACK);
#else
                             house_humidity_48x48, 48, 48, GxEPD_BLACK);
#endif

  // current weather data labels
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_LDY + WI_DY * PosY, TXT_INDOOR_HUMIDITY, LEFT);

  // indoor humidity
#ifdef DISP_3C_E420
  display.setFont(&FONT_7pt8b);
#else
  display.setFont(&FONT_12pt8b);
#endif
  if (!std::isnan(inHumidity))
  {
    dataStr = String(static_cast<int>(std::round(inHumidity)));
  }
  else
  {
    dataStr = "--";
  }
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_DDY + WI_DY * PosY, dataStr, LEFT, ACCENT_COLOR);
#ifdef DISP_3C_E420
  display.setFont(&FONT_5pt8b);
#else
  display.setFont(&FONT_8pt8b);
#endif
  drawString(display.getCursorX(), WI_Y0 + WI_DDY + WI_DY * PosY, "%", LEFT, ACCENT_COLOR);
  return;
}
#endif
// end drawCurrentInHumidity

// drawCurrentMoonrise
#ifdef POS_MOONRISE
void drawCurrentMoonrise(const owm_daily_t &today)
{
  String dataStr, unitStr;
  int PosX = POS_MOONRISE % 2;
  int PosY = static_cast<int>(POS_MOONRISE / 2);

  // icons
  display.drawInvertedBitmap(WI_COL * PosX, WI_Y0 + WI_DY * PosY,
#ifdef DISP_3C_E420
                             wi_moonrise_24x24, 24, 24, GxEPD_BLACK);
#else
                             wi_moonrise_48x48, 48, 48, GxEPD_BLACK);
#endif

  // labels
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_LDY + WI_DY * PosY, TXT_MOONRISE, LEFT);

  // moonrise
#ifdef DISP_3C_E420
  display.setFont(&FONT_7pt8b);
#else
  display.setFont(&FONT_12pt8b);
#endif
  char timeBuffer[12] = {}; // big enough to accommodate "hh:mm:ss am"
  time_t ts = today.moonrise;
  tm *timeInfo = localtime(&ts);
  _strftime(timeBuffer, sizeof(timeBuffer), TIME_FORMAT, timeInfo);
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_DDY + WI_DY * PosY, timeBuffer, LEFT, ACCENT_COLOR);

  return;
}
# endif
// end drawCurrentMoonrise

// drawCurrentMoonset
#ifdef POS_MOONSET
void drawCurrentMoonset(const owm_daily_t &today)
{
  String dataStr, unitStr;
  int PosX = (POS_MOONSET % 2);
  int PosY = static_cast<int>(POS_MOONSET / 2);
  // icons
  display.drawInvertedBitmap(WI_COL * PosX, WI_Y0 + WI_DY * PosY,
#ifdef DISP_3C_E420
                             wi_moonset_24x24, 24, 24, GxEPD_BLACK);
#else
                             wi_moonset_48x48, 48, 48, GxEPD_BLACK);
#endif

  // labels
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_LDY + WI_DY * PosY, TXT_MOONSET, LEFT, ACCENT_COLOR);

  // moonset
#ifdef DISP_3C_E420
  display.setFont(&FONT_7pt8b);
#else
  display.setFont(&FONT_12pt8b);
#endif
  char timeBuffer[12] = {}; // big enough to accommodate "hh:mm:ss am"
  time_t ts = today.moonset;
  tm *timeInfo = localtime(&ts);
  _strftime(timeBuffer, sizeof(timeBuffer), TIME_FORMAT, timeInfo);
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_DDY + WI_DY * PosY, timeBuffer, LEFT, ACCENT_COLOR);

  return;
}
#endif
// end drawCurrentMoonset

// drawCurrentMoonphase
#ifdef POS_MOONPHASE
void drawCurrentMoonphase(const owm_daily_t &daily)
{
  String dataStr, unitStr;
  int PosX = (POS_MOONPHASE % 2);
  int PosY = static_cast<int>(POS_MOONPHASE / 2);

  // icons
  display.drawInvertedBitmap(WI_COL * PosX, WI_Y0 + WI_DY * PosY,
#ifdef DISP_3C_E420
                             getMoonPhaseBitmap24(daily), 24, 24, GxEPD_BLACK);
#else
                             getMoonPhaseBitmap48(daily), 48, 48, GxEPD_BLACK);
#endif

  // labels
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_LDY + WI_DY * PosY, TXT_MOONPHASE, LEFT, ACCENT_COLOR);

  // moonphase
  const int sp = 8;
  dataStr = String(getMoonPhaseStr(daily));
  int max_w = (WI_COL + (PosX * WI_COL) - sp) - (WI_LOFF + (PosX * WI_COL));
#ifdef DISP_3C_E420
  display.setFont(&FONT_5pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  if (getStringWidth(dataStr) <= max_w)
  { // Fits on a single line, draw along bottom
    drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_DDY + WI_DY * PosY,
               dataStr, LEFT, ACCENT_COLOR);
  }
  else
  { // use smaller font
    display.setFont(&FONT_5pt8b);
    if (getStringWidth(dataStr) <= max_w)
    { // Fits on a single line with smaller font, draw along bottom
      drawString(WI_LOFF + (WI_COL * PosX),
                 WI_Y0 + WI_DDY + WI_DY * PosY,
                 dataStr, LEFT, ACCENT_COLOR);
    }
    else
    { // Does not fit on a single line, draw higher to allow room for 2nd line
      drawMultiLnString(WI_LOFF + (WI_COL * PosX),
                        WI_Y0 + WI_DDY + WI_DY * PosY - 10,
                        dataStr, LEFT, max_w, 2, 10, ACCENT_COLOR);
    }
  }

  return;
}
#endif
// end drawCurrentMoonphase

// drawCurrentDewpoint
#ifdef POS_DEWPOINT
void drawCurrentDewpoint(const owm_current_t &current)
{
  String dataStr, unitStr;
  int PosX = (POS_DEWPOINT % 2);
  int PosY = static_cast<int>(POS_DEWPOINT / 2);
  
  // icons
  display.drawInvertedBitmap(WI_COL * PosX, WI_Y0 + WI_DY * PosY,
#ifdef DISP_3C_E420
                             wi_thermometer_24x24, 24, 24, GxEPD_BLACK);
  display.drawInvertedBitmap(WI_COL * PosX + 24 - 12, WI_Y0 + WI_DY * PosY + 4,
                             wi_raindrops_16x16, 16, 16, GxEPD_BLACK);
#else
                             wi_thermometer_48x48, 48, 48, GxEPD_BLACK);
  display.drawInvertedBitmap(WI_COL * PosX + 48 - 24, WI_Y0 + WI_DY * PosY + 4,
                             wi_raindrops_24x24, 24, 24, GxEPD_BLACK);
#endif

  // labels
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_7pt8b);
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_LDY + WI_DY * PosY, TXT_DEWPOINT, LEFT);

  // Dew point
#ifdef DISP_3C_E420
  display.setFont(&FONT_7pt8b);
#else
  display.setFont(&FONT_12pt8b);
#endif
  if (!std::isnan(current.dew_point))
  {
#ifdef UNITS_TEMP_KELVIN
  dataStr = String(std::round(current.dew_point * 10) / 10.0f, 1);
#endif
#ifdef UNITS_TEMP_CELSIUS
  dataStr = String(std::round(kelvin_to_celsius(current.dew_point) * 10) / 10.0f, 1);
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
  dataStr = String(static_cast<int>(
            std::round(kelvin_to_fahrenheit(current.dew_point))));
#endif
  }
  else
  {
    dataStr = "--";
  }
#if defined(UNITS_TEMP_CELSIUS) || defined(UNITS_TEMP_FAHRENHEIT)
  dataStr += "\260";
#endif
  drawString(WI_LOFF + (WI_COL * PosX), WI_Y0 + WI_DDY + WI_DY * PosY, dataStr, LEFT);
  return;
} 
#endif
// end drawCurrentDewpoint

//End defining functions for left panel.

/* This function is responsible for drawing the current conditions and
 * associated icons.
 */
void drawCurrentConditions(const owm_current_t &current,
                           const owm_daily_t &today,
                           const owm_resp_air_pollution_t &owm_air_pollution,
                           float inTemp, float inHumidity)
{
  String dataStr, unitStr;
  // current weather icon
#ifdef DISP_3C_E420
  display.drawInvertedBitmap(0, 0,
                             getCurrentConditionsBitmap96(current, today),
                             96, 96, GxEPD_BLACK);
#else
  display.drawInvertedBitmap(0, 0,
                             getCurrentConditionsBitmap196(current, today),
                             196, 196, GxEPD_BLACK);
#endif

  // current temp
#ifdef UNITS_TEMP_KELVIN
  dataStr = String(static_cast<int>(std::round(current.temp)));
  unitStr = TXT_UNITS_TEMP_KELVIN;
#endif
#ifdef UNITS_TEMP_CELSIUS
  dataStr = String(static_cast<int>(
            std::round(kelvin_to_celsius(current.temp))));
  unitStr = TXT_UNITS_TEMP_CELSIUS;
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
  dataStr = String(static_cast<int>(
            std::round(kelvin_to_fahrenheit(current.temp))));
  unitStr = TXT_UNITS_TEMP_FAHRENHEIT;
#endif
  // FONT_**_temperature fonts only have the character set used for displaying
  // temperature (0123456789.-\260)
#ifdef DISP_3C_E420
  display.setFont(&FONT_18pt8b);
  drawString(96 + 30 - 10, 96 / 2 + 25 / 2, dataStr, CENTER);
  display.setFont(&FONT_7pt8b);
  drawString(display.getCursorX(), 96 / 2 - 25 / 2 + 10, unitStr, LEFT);
#else
  display.setFont(&FONT_48pt8b_temperature);
  #ifndef DISP_BW_V1
    drawString(196 + 164 / 2 - 20, 196 / 2 + 69 / 2, dataStr, CENTER);
  #elif defined(DISP_BW_V1)
    drawString(156 + 164 / 2 - 20, 196 / 2 + 69 / 2, dataStr, CENTER);
  #endif
  display.setFont(&FONT_14pt8b);
  drawString(display.getCursorX(), 196 / 2 - 69 / 2 + 20, unitStr, LEFT);
#endif

  // current feels like
#ifdef UNITS_TEMP_KELVIN
  dataStr = String(TXT_FEELS_LIKE) + ' '
            + String(static_cast<int>(std::round(current.feels_like)));
#endif
#ifdef UNITS_TEMP_CELSIUS
  dataStr = String(TXT_FEELS_LIKE) + ' '
            + String(static_cast<int>(std::round(
                     kelvin_to_celsius(current.feels_like))))
            + '\260';
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
  dataStr = String(TXT_FEELS_LIKE) + ' '
            + String(static_cast<int>(std::round(
                     kelvin_to_fahrenheit(current.feels_like))))
            + '\260';
#endif
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
  drawString(96 + 60 / 2, 96 / 2 + 25 / 2 + 12 / 2 + 17 / 2, dataStr, CENTER);
#else
  display.setFont(&FONT_12pt8b);
  #ifndef DISP_BW_V1
    drawString(196 + 164 / 2, 98 + 69 / 2 + 12 + 17, dataStr, CENTER);
  #elif defined(DISP_BW_V1)
    drawString(156 + 164 / 2, 98 + 69 / 2 + 12 + 17, dataStr, CENTER);
  #endif
#endif
  // line dividing top and bottom display areas
  // display.drawLine(0, 196, DISP_WIDTH - 1, 196, GxEPD_BLACK);

  // draw current data of the left panel

    # ifdef POS_SUNRISE
     drawCurrentSunrise(current);
    # endif

    # ifdef POS_SUNSET
      drawCurrentSunset(current);
    # endif

    # ifdef POS_WIND
      drawCurrentWind(current);
    # endif

    # ifdef POS_HUMIDITY
      drawCurrentHumidity(current);
    # endif

    # ifdef POS_UVI
      drawCurrentUVI(current);
    # endif

    # ifdef POS_PRESSURE
      drawCurrentPressure(current);
    # endif

    # ifdef POS_VISIBILITY
      drawCurrentVisibility(current);
    # endif

    # ifdef POS_AIR_QULITY
      drawCurrentAirQuality(owm_air_pollution);
    # endif

    # ifdef POS_INTEMP
      drawCurrentInTemp(inTemp);
    # endif

    # ifdef POS_INHUMIDITY
      drawCurrentInHumidity(inHumidity);
    # endif

    # ifdef POS_MOONRISE
     drawCurrentMoonrise(today);
    # endif

    # ifdef POS_MOONSET
      drawCurrentMoonset(today);
    # endif

    # ifdef POS_MOONPHASE
      drawCurrentMoonphase(today);
    # endif
  
    # ifdef POS_DEWPOINT
      drawCurrentDewpoint(current);
    # endif
  
    // end drawing left panel

  return;
} // end drawCurrentConditions

/* This function is responsible for drawing the five day forecast.
 */
void drawForecast(const owm_daily_t *daily, tm timeInfo)
{
  // 5 day, forecast
  String hiStr, loStr;
  String dataStr, unitStr;
  for (int i = 0; i < 5; ++i)
  {
#ifdef DISP_3C_E420
    int x = 180 + (i * 43);
#elif !defined(DISP_BW_V1)
    int x = 398 + (i * 82);
#elif defined(DISP_BW_V1)
    int x = 318 + (i * 64);
#endif
    // icons
#ifdef DISP_3C_E420
    display.drawInvertedBitmap(x, 46 + 69 / 4 - 32 / 2 - 6 / 2,
                               getDailyForecastBitmap32(daily[i]),
                               32, 32, GxEPD_BLACK);
#else
    display.drawInvertedBitmap(x, 98 + 69 / 2 - 32 - 6,
                               getDailyForecastBitmap64(daily[i]),
                               64, 64, GxEPD_BLACK);
#endif
    // day of week label
#ifdef DISP_3C_E420
    display.setFont(&FONT_6pt8b);
#else
    display.setFont(&FONT_11pt8b);
#endif
    char dayBuffer[8] = {};
    _strftime(dayBuffer, sizeof(dayBuffer), "%a", &timeInfo); // abbrv'd day
#ifdef DISP_3C_E420
    drawString(x + 15, 46 + 69 / 4 - 32 / 2 - 24 / 2 - 6 / 2 + 16 / 2, dayBuffer, CENTER);
#else
    drawString(x + 31 - 2, 98 + 69 / 2 - 32 - 26 - 6 + 16, dayBuffer, CENTER);
#endif
    timeInfo.tm_wday = (timeInfo.tm_wday + 1) % 7; // increment to next day

    // high | low
#ifdef DISP_3C_E420
    display.setFont(&FONT_5pt8b);
#else
    display.setFont(&FONT_8pt8b);
#endif
#ifdef DISP_3C_E420
    drawString(x + 15, 46 + 69 / 4 + 38 / 2 - 6 / 2 + 12 / 2, "|", CENTER);
#else
    drawString(x + 31, 98 + 69 / 2 + 38 - 6 + 12, "|", CENTER);
#endif
#ifdef UNITS_TEMP_KELVIN
    hiStr = String(static_cast<int>(std::round(daily[i].temp.max)));
    loStr = String(static_cast<int>(std::round(daily[i].temp.min)));
#endif
#ifdef UNITS_TEMP_CELSIUS
    hiStr = String(static_cast<int>(
                std::round(kelvin_to_celsius(daily[i].temp.max)))) +
            "\260";
    loStr = String(static_cast<int>(
                std::round(kelvin_to_celsius(daily[i].temp.min)))) +
            "\260";
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
    hiStr = String(static_cast<int>(
                std::round(kelvin_to_fahrenheit(daily[i].temp.max)))) +
            "\260";
    loStr = String(static_cast<int>(
                std::round(kelvin_to_fahrenheit(daily[i].temp.min)))) +
            "\260";
#endif
#ifdef TEMP_ORDER_HL
  #ifdef DISP_3C_E420
    drawString(x + 15 - 2, 46 + 69 / 4 + 38 / 2 - 6 / 2 + 12 / 2, hiStr, RIGHT);
    drawString(x + 15 + 2, 46 + 69 / 4 + 38 / 2 - 6 / 2 + 12 / 2, loStr, LEFT, ACCENT_COLOR);
  #else
    drawString(x + 31 - 4, 98 + 69 / 2 + 38 - 6 + 12, hiStr, RIGHT);
    drawString(x + 31 + 5, 98 + 69 / 2 + 38 - 6 + 12, loStr, LEFT);
  #endif
#endif
#ifdef TEMP_ORDER_LH
  #ifdef DISP_3C_E420
    drawString(x + 15 - 2, 46 + 69 / 4 + 38 / 2 - 6 / 2 + 12 / 2, loStr, RIGHT);
    drawString(x + 15 + 2, 46 + 69 / 4 + 38 / 2 - 6 / 2 + 12 / 2, hiStr, LEFT, ACCENT_COLOR);
  #else
    drawString(x + 31 - 4, 98 + 69 / 2 + 38 - 6 + 12, loStr, RIGHT);
    drawString(x + 31 + 5, 98 + 69 / 2 + 38 - 6 + 12, hiStr, LEFT);
  #endif
#endif

// daily forecast precipitation
#if DISPLAY_DAILY_PRECIP
    float dailyPrecip;
#if defined(UNITS_DAILY_PRECIP_POP)
    dailyPrecip = daily[i].pop * 100;
    dataStr = String(static_cast<int>(dailyPrecip));
    unitStr = "%";
#else
    dailyPrecip = daily[i].snow + daily[i].rain;
#if defined(UNITS_DAILY_PRECIP_MILLIMETERS)
    // Round up to nearest mm
    dailyPrecip = std::round(dailyPrecip);
    dataStr = String(static_cast<int>(dailyPrecip));
    unitStr = String(" ") + TXT_UNITS_PRECIP_MILLIMETERS;
#elif defined(UNITS_DAILY_PRECIP_CENTIMETERS)
    // Round up to nearest 0.1 cm
    dailyPrecip = millimeters_to_centimeters(dailyPrecip);
    dailyPrecip = std::round(dailyPrecip * 10) / 10.0f;
    dataStr = String(dailyPrecip, 1);
    unitStr = String(" ") + TXT_UNITS_PRECIP_CENTIMETERS;
#elif defined(UNITS_DAILY_PRECIP_INCHES)
    // Round up to nearest 0.1 inch
    dailyPrecip = millimeters_to_inches(dailyPrecip);
    dailyPrecip = std::round(dailyPrecip * 10) / 10.0f;
    dataStr = String(dailyPrecip, 1);
    unitStr = String(" ") + TXT_UNITS_PRECIP_INCHES;
#endif
#endif
#if (DISPLAY_DAILY_PRECIP == 2) // smart
      if (dailyPrecip > 0.0f)
      {
#endif
        display.setFont(&FONT_6pt8b);
#ifdef DISP_3C_E420
        drawString(x + 15, 103, dataStr + unitStr, CENTER);
#else
        drawString(x + 31, 98 + 69 / 2 + 38 - 6 + 26,
                   dataStr + unitStr, CENTER);
#endif
#if (DISPLAY_DAILY_PRECIP == 2) // smart
      }
#endif
#endif // DISPLAY_DAILY_PRECIP
    }

    return;
  } // end drawForecast

  /* This function is responsible for drawing the current alerts if any.
   * Up to 2 alerts can be drawn.
   */
  void drawAlerts(std::vector<owm_alerts_t> & alerts,
                  const String &city, const String &date)
  {
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] alerts.size()    : " + String(alerts.size()));
#endif
  if (alerts.size() == 0)
  { // no alerts to draw
    return;
  }

  int *ignore_list = (int *) calloc(alerts.size(), sizeof(*ignore_list));
  int *alert_indices = (int *) calloc(alerts.size(), sizeof(*alert_indices));
  if (!ignore_list || !alert_indices)
  {
    Serial.println("Error: Failed to allocate memory while handling alerts.");
    free(ignore_list);
    free(alert_indices);
    return;
  }

  // Converts all event text and tags to lowercase, removes extra information,
  // and filters out redundant alerts of lesser urgency.
  filterAlerts(alerts, ignore_list);

  // limit alert text width so that is does not run into the location or date
  // strings
  display.setFont(&FONT_16pt8b);
  int city_w = getStringWidth(city);
  display.setFont(&FONT_12pt8b);
  int date_w = getStringWidth(date);
  int max_w = DISP_WIDTH - 2 - std::max(city_w, date_w) - (196 + 4) - 8;

  // find indices of valid alerts
  int num_valid_alerts = 0;
#if DEBUG_LEVEL >= 1
  Serial.print("[debug] ignore_list      : [ ");
#endif
  for (int i = 0; i < alerts.size(); ++i)
  {
#if DEBUG_LEVEL >= 1
    Serial.print(String(ignore_list[i]) + " ");
#endif
    if (!ignore_list[i])
    {
      alert_indices[num_valid_alerts] = i;
      ++num_valid_alerts;
    }
  }
#if DEBUG_LEVEL >= 1
  Serial.println("]\n[debug] num_valid_alerts : " + String(num_valid_alerts));
#endif

  if (num_valid_alerts == 1)
  { // 1 alert
    // adjust max width to for 48x48 icons
    max_w -= 48;

    owm_alerts_t &cur_alert = alerts[alert_indices[0]];
    display.drawInvertedBitmap(196, 8, getAlertBitmap48(cur_alert), 48, 48,
                               ACCENT_COLOR);
    // must be called after getAlertBitmap
    toTitleCase(cur_alert.event);

    display.setFont(&FONT_14pt8b);
    if (getStringWidth(cur_alert.event) <= max_w)
    { // Fits on a single line, draw along bottom
      drawString(196 + 48 + 4, 24 + 8 - 12 + 20 + 1, cur_alert.event, LEFT);
    }
    else
    { // use smaller font
      display.setFont(&FONT_12pt8b);
      if (getStringWidth(cur_alert.event) <= max_w)
      { // Fits on a single line with smaller font, draw along bottom
        drawString(196 + 48 + 4, 24 + 8 - 12 + 17 + 1, cur_alert.event, LEFT);
      }
      else
      { // Does not fit on a single line, draw higher to allow room for 2nd line
        drawMultiLnString(196 + 48 + 4, 24 + 8 - 12 + 17 - 11,
                          cur_alert.event, LEFT, max_w, 2, 23);
      }
    }
  } // end 1 alert
  else
  { // 2 alerts
    // adjust max width to for 32x32 icons
    max_w -= 32;

    display.setFont(&FONT_12pt8b);
    for (int i = 0; i < 2; ++i)
    {
      owm_alerts_t &cur_alert = alerts[alert_indices[i]];

      display.drawInvertedBitmap(196, (i * 32), getAlertBitmap32(cur_alert),
                                 32, 32, ACCENT_COLOR);
      // must be called after getAlertBitmap
      toTitleCase(cur_alert.event);

      drawMultiLnString(196 + 32 + 3, 5 + 17 + (i * 32),
                        cur_alert.event, LEFT, max_w, 1, 0);
    } // end for-loop
  } // end 2 alerts

  free(ignore_list);
  free(alert_indices);

  return;
} // end drawAlerts

/* This function is responsible for drawing the city string and date
 * information in the top right corner.
 */
void drawLocationDate(const String &city, const String &date)
{
  // location, date
#ifdef DISP_3C_E420
  // 4.2in (400x300): use compact fonts in the top-right corner so the city
  // and date do not collide with the 5-day forecast row below them.
  display.setFont(&FONT_8pt8b);
  drawString(DISP_WIDTH - 2, 12, city, RIGHT, ACCENT_COLOR);
  display.setFont(&FONT_6pt8b);
  drawString(DISP_WIDTH - 2, 25, date, RIGHT);
#else
  display.setFont(&FONT_16pt8b);
  drawString(DISP_WIDTH - 2, 23, city, RIGHT, ACCENT_COLOR);
  display.setFont(&FONT_12pt8b);
  drawString(DISP_WIDTH - 2, 30 + 4 + 17, date, RIGHT);
#endif
  return;
} // end drawLocationDate

/* The % operator in C++ is not a true modulo operator but it instead a
 * remainder operator. The remainder operator and modulo operator are equivalent
 * for positive numbers, but not for negatives. The follow implementation of the
 * modulo operator works for +/-a and +b.
 */
inline int modulo(int a, int b)
{
  const int result = a % b;
  return result >= 0 ? result : result + b;
}

/* Convert temperature in kelvin to the display y coordinate to be plotted.
 */
int kelvin_to_plot_y(float kelvin, int tempBoundMin, float yPxPerUnit,
                     int yBoundMin)
{
#ifdef UNITS_TEMP_KELVIN
  return static_cast<int>(std::round(
    yBoundMin - (yPxPerUnit * (kelvin - tempBoundMin)) ));
#endif
#ifdef UNITS_TEMP_CELSIUS
  return static_cast<int>(std::round(
    yBoundMin - (yPxPerUnit * (kelvin_to_celsius(kelvin) - tempBoundMin)) ));
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
  return static_cast<int>(std::round(
    yBoundMin - (yPxPerUnit * (kelvin_to_fahrenheit(kelvin) - tempBoundMin)) ));
#endif
}

/* This function is responsible for drawing the outlook graph for the specified
 * number of hours(up to 48).
 */
void drawOutlookGraph(const owm_hourly_t *hourly, const owm_daily_t *daily,
                      tm timeInfo)
{
#ifdef DISP_3C_E420
  const int xPos0 = 180;
  int xPos1 = DISP_WIDTH;
  const int yPos0 = 108;
  // leave room below the graph for the x-axis time labels and the status bar
  const int yPos1 = DISP_HEIGHT - 46;
#else
  const int xPos0 = 350;
  int xPos1 = DISP_WIDTH;
  const int yPos0 = 216;
  const int yPos1 = DISP_HEIGHT - 46;
#endif

  // calculate y max/min and intervals
  int yMajorTicks = 5;
#ifdef UNITS_TEMP_KELVIN
  float tempMin = hourly[0].temp;
#endif
#ifdef UNITS_TEMP_CELSIUS
  float tempMin = kelvin_to_celsius(hourly[0].temp);
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
  float tempMin = kelvin_to_fahrenheit(hourly[0].temp);
#endif
  float tempMax = tempMin;
#ifdef UNITS_HOURLY_PRECIP_POP
  float precipMax = hourly[0].pop;
#else
  float precipMax = hourly[0].rain_1h + hourly[0].snow_1h;
#endif
  int yTempMajorTicks = 5;
  float newTemp = 0;
  for (int i = 1; i < HOURLY_GRAPH_MAX; ++i)
  {
#ifdef UNITS_TEMP_KELVIN
    newTemp = hourly[i].temp;
#endif
#ifdef UNITS_TEMP_CELSIUS
    newTemp = kelvin_to_celsius(hourly[i].temp);
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
    newTemp = kelvin_to_fahrenheit(hourly[i].temp);
#endif
    tempMin = std::min(tempMin, newTemp);
    tempMax = std::max(tempMax, newTemp);
#ifdef UNITS_HOURLY_PRECIP_POP
    precipMax = std::max<float>(precipMax, hourly[i].pop);
#else
    precipMax = std::max<float>(
                precipMax, hourly[i].rain_1h + hourly[i].snow_1h);
#endif
  }
  int tempBoundMin = static_cast<int>(tempMin - 1)
                      - modulo(static_cast<int>(tempMin - 1), yTempMajorTicks);
  int tempBoundMax = static_cast<int>(tempMax + 1)
   + (yTempMajorTicks - modulo(static_cast<int>(tempMax + 1), yTempMajorTicks));

  // while we have to many major ticks then increase the step
  while ((tempBoundMax - tempBoundMin) / yTempMajorTicks > yMajorTicks)
  {
    yTempMajorTicks += 5;
    tempBoundMin = static_cast<int>(tempMin - 1)
                      - modulo(static_cast<int>(tempMin - 1), yTempMajorTicks);
    tempBoundMax = static_cast<int>(tempMax + 1) + (yTempMajorTicks
                      - modulo(static_cast<int>(tempMax + 1), yTempMajorTicks));
  }
  // while we have not enough major ticks, add to either bound
  while ((tempBoundMax - tempBoundMin) / yTempMajorTicks < yMajorTicks)
  {
    // add to whatever bound is closer to the actual min/max
    if (tempMin - tempBoundMin <= tempBoundMax - tempMax)
    {
      tempBoundMin -= yTempMajorTicks;
    }
    else
    {
      tempBoundMax += yTempMajorTicks;
    }
  }

#ifdef UNITS_HOURLY_PRECIP_POP
  xPos1 = DISP_WIDTH - 23;
  float precipBoundMax;
  if (precipMax > 0)
  {
    precipBoundMax = 100.0f;
  }
  else
  {
    precipBoundMax = 0.0f;
  }
#else
#ifdef UNITS_HOURLY_PRECIP_MILLIMETERS
  xPos1 = DISP_WIDTH - 24;
  float precipBoundMax = std::ceil(precipMax); // Round up to nearest mm
  int yPrecipMajorTickDecimals = (precipBoundMax < 10);
#endif
#ifdef UNITS_HOURLY_PRECIP_CENTIMETERS
  xPos1 = DISP_WIDTH - 25;
  precipMax = millimeters_to_centimeters(precipMax);
  // Round up to nearest 0.1 cm
  float precipBoundMax = std::ceil(precipMax * 10) / 10.0f;
  int yPrecipMajorTickDecimals;
  if (precipBoundMax < 1)
  {
    yPrecipMajorTickDecimals = 2;
    if (precipBoundMax > 0)
    {
      xPos1 -= 6; // needs extra room
    }
  }
  else if (precipBoundMax < 10)
  {
    yPrecipMajorTickDecimals = 1;
  }
  else
  {
    yPrecipMajorTickDecimals = 0;
  }
#endif
#ifdef UNITS_HOURLY_PRECIP_INCHES
  xPos1 = DISP_WIDTH - 25;
  precipMax = millimeters_to_inches(precipMax);
  // Round up to nearest 0.1 inch
  float precipBoundMax = std::ceil(precipMax * 10) / 10.0f;
  int yPrecipMajorTickDecimals;
  if (precipBoundMax < 1)
  {
    yPrecipMajorTickDecimals = 2;
  }
  else if (precipBoundMax < 10)
  {
    yPrecipMajorTickDecimals = 1;
  }
  else
  {
    yPrecipMajorTickDecimals = 0;
  }
#endif
  float yPrecipMajorTickValue = precipBoundMax / yMajorTicks;
  float precipRoundingMultiplier = std::pow(10.f, yPrecipMajorTickDecimals);
#endif

  if (precipBoundMax > 0)
  { // fill need extra room for labels
    xPos1 -= 23;
  }

  // draw x axis
  display.drawLine(xPos0, yPos1    , xPos1, yPos1    , GxEPD_BLACK);
  display.drawLine(xPos0, yPos1 - 1, xPos1, yPos1 - 1, GxEPD_BLACK);

  // draw y axis
  float yInterval = (yPos1 - yPos0) / static_cast<float>(yMajorTicks);
  for (int i = 0; i <= yMajorTicks; ++i)
  {
    String dataStr;
    int yTick = static_cast<int>(yPos0 + (i * yInterval));
    #ifdef DISP_3C_E420
        display.setFont(&FONT_6pt8b);
    #else
      display.setFont(&FONT_8pt8b);
    #endif
    // Temperature
    dataStr = String(tempBoundMax - (i * yTempMajorTicks));
#if defined(UNITS_TEMP_CELSIUS) || defined(UNITS_TEMP_FAHRENHEIT)
    dataStr += "\260";
#endif
    drawString(xPos0 - 8, yTick + 4, dataStr, RIGHT, ACCENT_COLOR);

    if (precipBoundMax > 0)
    { // don't labels if precip is 0
#ifdef UNITS_HOURLY_PRECIP_POP
      // PoP
      dataStr = String(100 - (i * 20));
      String precipUnit = "%";
#else
      // Precipitation volume
      float precipTick = precipBoundMax - (i * yPrecipMajorTickValue);
      precipTick = std::round(precipTick * precipRoundingMultiplier)
                              / precipRoundingMultiplier;
      dataStr = String(precipTick, yPrecipMajorTickDecimals);
#ifdef UNITS_HOURLY_PRECIP_MILLIMETERS
      String precipUnit = String(" ") + TXT_UNITS_PRECIP_MILLIMETERS;
#endif
#ifdef UNITS_HOURLY_PRECIP_CENTIMETERS
      String precipUnit = String(" ") + TXT_UNITS_PRECIP_CENTIMETERS;
#endif
#ifdef UNITS_HOURLY_PRECIP_INCHES
      String precipUnit = String(" ") + TXT_UNITS_PRECIP_INCHES;
#endif
#endif

      drawString(xPos1 + 8, yTick + 4, dataStr, LEFT);
      display.setFont(&FONT_5pt8b);
      drawString(display.getCursorX(), yTick + 4, precipUnit, LEFT);
    } // end draw labels if precip is >0

    // draw dotted line
    if (i < yMajorTicks)
    {
      for (int x = xPos0; x <= xPos1 + 1; x += 3)
      {
        display.drawPixel(x, yTick + (yTick % 2), GxEPD_BLACK);
      }
    }
  }

  int xMaxTicks = 8;
  int hourInterval = static_cast<int>(ceil(HOURLY_GRAPH_MAX
                                           / static_cast<float>(xMaxTicks)));
  float xInterval = (xPos1 - xPos0 - 1) / static_cast<float>(HOURLY_GRAPH_MAX);
  #ifdef DISP_3C_E420
      display.setFont(&FONT_6pt8b);
  #else
      display.setFont(&FONT_8pt8b);
  #endif

  // precalculate all x and y coordinates for temperature values
  float yPxPerUnit = (yPos1 - yPos0)
                     / static_cast<float>(tempBoundMax - tempBoundMin);
  std::vector<int> x_t;
  std::vector<int> y_t;
  x_t.reserve(HOURLY_GRAPH_MAX);
  y_t.reserve(HOURLY_GRAPH_MAX);
    for (int i = 0; i < HOURLY_GRAPH_MAX; ++i)
  {
    y_t[i] = kelvin_to_plot_y(hourly[i].temp, tempBoundMin, yPxPerUnit, yPos1);
    x_t[i] = static_cast<int>(std::round(xPos0 + (i * xInterval)
                                          + (0.5 * xInterval) ));
  }

#if DISPLAY_HOURLY_ICONS
  int day_idx = 0;
#endif
#ifdef DISP_3C_E420
  display.setFont(&FONT_6pt8b);
#else
  display.setFont(&FONT_8pt8b);
#endif
  for (int i = 0; i < HOURLY_GRAPH_MAX; ++i)
  {
    int xTick = static_cast<int>(xPos0 + (i * xInterval));
    int x0_t, x1_t, y0_t, y1_t;

    if (i > 0)
    {
      // temperature
      x0_t = x_t[i - 1];
      x1_t = x_t[i    ];
      y0_t = y_t[i - 1];
      y1_t = y_t[i    ];
      // graph temperature
      display.drawLine(x0_t    , y0_t    , x1_t    , y1_t    , ACCENT_COLOR);
      display.drawLine(x0_t    , y0_t + 1, x1_t    , y1_t + 1, ACCENT_COLOR);
      display.drawLine(x0_t - 1, y0_t    , x1_t - 1, y1_t    , ACCENT_COLOR);

      // draw hourly bitmap
#if DISPLAY_HOURLY_ICONS
      if (daily[day_idx].dt + 86400 <= hourly[i].dt) {
        ++day_idx;
      }
      if ((i % hourInterval) == 0) // skip first and last tick
      {
        int y_b = INT_MAX;
        // find the highest (lowest in coordinate value) temperature point that
        // exists within the width of the icon.
        // find closest point above the temperature line where the icon won't
        // interect the temperature line.
        // y = mx + b
        int span = static_cast<int>(std::round(16 / xInterval));
        int l_idx = std::max(i - 1 - span, 0);
        int r_idx = std::min(i + span, HOURLY_GRAPH_MAX - 1);
        // left intersecting slope
        float m_l = (y_t[l_idx + 1] - y_t[l_idx]) / xInterval;
        int x_l = xTick - 16 - x_t[l_idx];
        int y_l = static_cast<int>(std::round(m_l * x_l + y_t[l_idx]));
        y_b = std::min(y_l, y_b);
        // right intersecting slope
        float m_r = (y_t[r_idx] - y_t[r_idx - 1]) / xInterval;
        int x_r = xTick + 16 - x_t[r_idx - 1];
        int y_r = static_cast<int>(std::round(m_r * x_r + y_t[r_idx - 1]));
        y_b = std::min(y_r, y_b);
        // any peaks in between
        for (int idx = l_idx + 1; idx < r_idx; ++idx)
        {
          y_b = std::min(y_t[idx], y_b);
        }
        const uint8_t *bitmap = getHourlyForecastBitmap32(hourly[i],
                                                          daily[day_idx]);
#ifdef DISP_3C_E420
        y_b = std::min(y_b - 6, yPos1 - 52);
        display.drawInvertedBitmap(xTick - 16, y_b - 32,
                                   bitmap, 32, 32, GxEPD_BLACK);
#else
        display.drawInvertedBitmap(xTick - 16, y_b - 32,
                                   bitmap, 32, 32, GxEPD_BLACK);
#endif
      }
#endif
    }

#ifdef UNITS_HOURLY_PRECIP_POP
    float precipVal = hourly[i].pop * 100;
#else
    float precipVal = hourly[i].rain_1h + hourly[i].snow_1h;
#ifdef UNITS_HOURLY_PRECIP_CENTIMETERS
    precipVal = millimeters_to_centimeters(precipVal);
#endif
#ifdef UNITS_HOURLY_PRECIP_INCHES
    precipVal = millimeters_to_inches(precipVal);
#endif
#endif

    x0_t = static_cast<int>(std::round( xPos0 + 1 + (i * xInterval)));
    x1_t = static_cast<int>(std::round( xPos0 + 1 + ((i + 1) * xInterval) ));
    yPxPerUnit = (yPos1 - yPos0) / precipBoundMax;
    y0_t = static_cast<int>(std::round( yPos1 - (yPxPerUnit * (precipVal)) ));
    y1_t = yPos1;

    // graph Precipitation
    for (int y = y1_t - 1; y > y0_t; y -= 2)
    {
      for (int x = x0_t + (x0_t % 2); x < x1_t; x += 2)
      {
        display.drawPixel(x, y, GxEPD_BLACK);
      }
    }

    if ((i % hourInterval) == 0)
    {
      // draw x tick marks
      display.drawLine(xTick    , yPos1 + 1, xTick    , yPos1 + 4, GxEPD_BLACK);
      display.drawLine(xTick + 1, yPos1 + 1, xTick + 1, yPos1 + 4, GxEPD_BLACK);
      // draw x axis labels
      char timeBuffer[12] = {}; // big enough to accommodate "hh:mm:ss am"
      time_t ts = hourly[i].dt;
      tm *timeInfo = localtime(&ts);
      _strftime(timeBuffer, sizeof(timeBuffer), HOUR_FORMAT, timeInfo);
      drawString(xTick, yPos1 + 1 + 12 + 4 + 3, timeBuffer, CENTER);
    }

  }

  // draw the last tick mark
  if ((HOURLY_GRAPH_MAX % hourInterval) == 0)
  {
    int xTick = static_cast<int>(
                std::round(xPos0 + (HOURLY_GRAPH_MAX * xInterval)));
    // draw x tick marks
    display.drawLine(xTick    , yPos1 + 1, xTick    , yPos1 + 4, GxEPD_BLACK);
    display.drawLine(xTick + 1, yPos1 + 1, xTick + 1, yPos1 + 4, GxEPD_BLACK);
    // draw x axis labels
    char timeBuffer[12] = {}; // big enough to accommodate "hh:mm:ss am"
    time_t ts = hourly[HOURLY_GRAPH_MAX - 1].dt + 3600;
    tm *timeInfo = localtime(&ts);
    _strftime(timeBuffer, sizeof(timeBuffer), HOUR_FORMAT, timeInfo);
    drawString(xTick, yPos1 + 1 + 12 + 4 + 3, timeBuffer, CENTER);
  }

  return;
} // end drawOutlookGraph

/* This function is responsible for drawing the status bar along the bottom of
 * the display.
 */
void drawStatusBar(const String &statusStr, const String &refreshTimeStr,
                   int rssi, uint32_t batVoltage)
{
  String dataStr;
  uint16_t dataColor = GxEPD_BLACK;
  display.setFont(&FONT_6pt8b);
  int pos = DISP_WIDTH - 2;
  const int sp = 2;

#if BATTERY_MONITORING
  // battery - (expecting 3.7v LiPo)
  uint32_t batPercent = calcBatPercent(batVoltage,
                                       MIN_BATTERY_VOLTAGE,
                                       MAX_BATTERY_VOLTAGE);
#if defined(DISP_3C_B) || defined(DISP_7C_F)
  if (batVoltage < WARN_BATTERY_VOLTAGE)
  {
    dataColor = ACCENT_COLOR;
  }
#endif
#if STATUS_BAR_EXTRAS_BAT_PERCENTAGE || STATUS_BAR_EXTRAS_BAT_VOLTAGE
  dataStr = "";
#if STATUS_BAR_EXTRAS_BAT_PERCENTAGE
  dataStr += String(batPercent) + "%";
#endif
#if STATUS_BAR_EXTRAS_BAT_VOLTAGE
  dataStr += " (" + String( std::round(batVoltage / 10.f) / 100.f, 2 ) + "v)";
#endif
  drawString(pos, DISP_HEIGHT - 1 - 2, dataStr, RIGHT, dataColor);
  pos -= getStringWidth(dataStr) + 1;
#endif
  pos -= 24;
  display.drawInvertedBitmap(pos, DISP_HEIGHT - 1 - 17,
                             getBatBitmap24(batPercent), 24, 24, dataColor);
  pos -= sp + 9;
#endif

  // WiFi
  dataColor = rssi >= -70 ? GxEPD_BLACK : ACCENT_COLOR;
#if STATUS_BAR_EXTRAS_WIFI_STRENGTH || STATUS_BAR_EXTRAS_WIFI_RSSI
  dataStr = "";
#if STATUS_BAR_EXTRAS_WIFI_STRENGTH
  dataStr += String(getWiFidesc(rssi));
#endif
#if STATUS_BAR_EXTRAS_WIFI_RSSI
  if (rssi != 0)
  {
    dataStr += " (" + String(rssi) + "dBm)";
  }
#endif
  drawString(pos, DISP_HEIGHT - 1 - 2, dataStr, RIGHT, dataColor);
  pos -= getStringWidth(dataStr) + 1;
#endif
  pos -= 18;
  display.drawInvertedBitmap(pos, DISP_HEIGHT - 1 - 13, getWiFiBitmap16(rssi),
                             16, 16, dataColor);
  pos -= sp + 8;

  // last refresh
  dataColor = GxEPD_BLACK;
  drawString(pos, DISP_HEIGHT - 1 - 2, refreshTimeStr, RIGHT, dataColor);
  pos -= getStringWidth(refreshTimeStr) + 25;
#ifdef DISP_3C_E420
  display.drawInvertedBitmap(pos, DISP_HEIGHT - 1 - 18, wi_refresh_24x24,
                             24, 24, dataColor);
  pos -= sp;
#else
  display.drawInvertedBitmap(pos, DISP_HEIGHT - 1 - 21, wi_refresh_32x32,
                             32, 32, dataColor);
  pos -= sp;
#endif

  // status
  dataColor = ACCENT_COLOR;
  if (!statusStr.isEmpty())
  {
    drawString(pos, DISP_HEIGHT - 1 - 2, statusStr, RIGHT, dataColor);
    pos -= getStringWidth(statusStr) + 24;
    display.drawInvertedBitmap(pos, DISP_HEIGHT - 1 - 18, error_icon_24x24,
                               24, 24, dataColor);
  }

  return;
} // end drawStatusBar

/* This function is responsible for drawing prominent error messages to the
 * screen.
 *
 * If error message line 2 (errMsgLn2) is empty, line 1 will be automatically
 * wrapped.
 */
void drawError(const uint8_t *bitmap,
               const String &errMsgLn1, const String &errMsgLn2)
{
#ifdef DISP_3C_E420
  const int iconSize = 96;
#else
  const int iconSize = 196;
#endif
#ifdef DISP_3C_E420
  display.setFont(&FONT_12pt8b);
  const int errLineH = 28;
  const int errY0    = DISP_HEIGHT / 2 + iconSize / 2 + 10;
#else
  display.setFont(&FONT_26pt8b);
  const int errLineH = 55;
  const int errY0    = DISP_HEIGHT / 2 + iconSize / 2 + 21;
#endif
  if (!errMsgLn2.isEmpty())
  {
    drawString(DISP_WIDTH / 2, errY0,               errMsgLn1, CENTER);
    drawString(DISP_WIDTH / 2, errY0 + errLineH,    errMsgLn2, CENTER);
  }
  else
  {
    drawMultiLnString(DISP_WIDTH / 2, errY0,
                      errMsgLn1, CENTER, DISP_WIDTH - 20, 2, errLineH);
  }
  display.drawInvertedBitmap(DISP_WIDTH / 2 - iconSize / 2,
                             DISP_HEIGHT / 2 - iconSize / 2 - 21,
                             bitmap, iconSize, iconSize, ACCENT_COLOR);
  return;
} // end drawError

/* =========================================================================
 * MULTI-SCREEN IMPLEMENTATION (400x300 E-PAPER)
 * Screen 0: Realtime Microclimate & Today's Weather Outlook
 * Screen 1: Historical Summary (Past 4 Days + Today from SD) & 24h Trend
 * Screen 2: Nautical & Wind Special (Lega Navale Torre del Greco)
 * ========================================================================= */

// Bottom Page Dots Indicator
void drawPageIndicator(uint8_t page)
{
  display.setFont(&FONT_6pt8b);
  int startX = 8;
  int startY = DISP_HEIGHT - 6;

  // Draw 3 dots
  for (int i = 0; i < 3; ++i)
  {
    if (i == page)
    {
      display.fillCircle(startX + (i * 12), startY - 3, 3, GxEPD_BLACK);
    }
    else
    {
      display.drawCircle(startX + (i * 12), startY - 3, 3, GxEPD_BLACK);
    }
  }

  char pageBuf[32];
  snprintf(pageBuf, sizeof(pageBuf), "P%d/3 [BOOT: cambio]", page + 1);
  drawString(startX + 40, startY, pageBuf, LEFT, GxEPD_BLACK);
}

// Draws a station's observation time right-aligned inside its card. Shows only
// HH:MM when the reading is from today, and "!" + red when it is stale.
static void drawStationTimestamp(int16_t xRight, int16_t y,
                                 const local_station_t &st, const tm &now)
{
  if (st.obs_epoch <= 0) return;
  tm obs;
  localtime_r(&st.obs_epoch, &obs);
  const bool stale = (time(nullptr) - st.obs_epoch) > STATION_STALE_SECONDS;
  char buf[24];
  if (obs.tm_yday == now.tm_yday && obs.tm_year == now.tm_year)
    snprintf(buf, sizeof(buf), "%s%02d:%02d", stale ? "! " : "", obs.tm_hour, obs.tm_min);
  else
    snprintf(buf, sizeof(buf), "%s%02d/%02d %02d:%02d", stale ? "! " : "",
             obs.tm_mday, obs.tm_mon + 1, obs.tm_hour, obs.tm_min);
  display.setFont(&FONT_5pt8b);
  drawString(xRight, y, buf, RIGHT, stale ? GxEPD_RED : GxEPD_BLACK);
}

// Bottom Status Bar
void drawCustomStatusBar(const weather_bundle_t &bundle, const tm &timeInfo)
{
  display.setFont(&FONT_6pt8b);
  int curX = DISP_WIDTH - 8;
  int curY = DISP_HEIGHT - 6;

  // Battery status
  if (bundle.battery_mv > 0)
  {
    uint32_t batPct = calcBatPercent(bundle.battery_mv, MIN_BATTERY_VOLTAGE, MAX_BATTERY_VOLTAGE);
    char batBuf[16];
    snprintf(batBuf, sizeof(batBuf), "%d%%", batPct);
    drawString(curX, curY, batBuf, RIGHT, GxEPD_BLACK);
    curX -= getStringWidth(batBuf) + 4;
    display.drawInvertedBitmap(curX - 16, DISP_HEIGHT - 17, getBatBitmap24(batPct), 24, 24, GxEPD_BLACK);
    curX -= 22;
  }

  // WiFi / Offline indicator
  if (bundle.is_offline)
  {
    char offBuf[32];
    tm syncTm;
    localtime_r(&bundle.last_sync_epoch, &syncTm);
    snprintf(offBuf, sizeof(offBuf), "OFFLINE (%02d:%02d)", syncTm.tm_hour, syncTm.tm_min);
    drawString(curX, curY, offBuf, RIGHT, GxEPD_RED);
    curX -= getStringWidth(offBuf) + 4;
    display.drawInvertedBitmap(curX - 16, DISP_HEIGHT - 15, error_icon_24x24, 24, 24, GxEPD_RED);
  }
  else
  {
    char syncBuf[24];
    snprintf(syncBuf, sizeof(syncBuf), "Sync %02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
    drawString(curX, curY, syncBuf, RIGHT, GxEPD_BLACK);
    curX -= getStringWidth(syncBuf) + 4;
    display.drawInvertedBitmap(curX - 16, DISP_HEIGHT - 14, getWiFiBitmap16(bundle.wifi_rssi), 16, 16, GxEPD_BLACK);
  }

  // Horizontal line separating content from status bar
  display.drawLine(4, DISP_HEIGHT - 22, DISP_WIDTH - 4, DISP_HEIGHT - 22, GxEPD_BLACK);
}

// Screen 0: Realtime Microclimate & Today's Weather Outlook
void drawScreen0_Dashboard(const weather_bundle_t &bundle, const tm &timeInfo)
{
  // 1. Header
  display.setFont(&FONT_9pt8b);
  drawString(8, 16, "TORRE DEL GRECO", LEFT, GxEPD_RED);
  display.setFont(&FONT_6pt8b);
  drawString(8, 28, "Microclima & Meteo Odierno", LEFT, GxEPD_BLACK);

  char dateStr[32];
  static const char* dayNamesFull[] = {"Dom", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab"};
  static const char* monNamesFull[] = {"Gen", "Feb", "Mar", "Apr", "Mag", "Giu", "Lug", "Ago", "Set", "Ott", "Nov", "Dic"};
  snprintf(dateStr, sizeof(dateStr), "%s %d %s %02d:%02d",
           dayNamesFull[timeInfo.tm_wday % 7], timeInfo.tm_mday, monNamesFull[timeInfo.tm_mon % 12],
           timeInfo.tm_hour, timeInfo.tm_min);
  drawString(DISP_WIDTH - 8, 20, dateStr, RIGHT, GxEPD_BLACK);
  display.drawLine(6, 33, DISP_WIDTH - 6, 33, GxEPD_BLACK);

  // 2. Left Column: Realtime Microclimate (X = 6 to 196)
  display.setFont(&FONT_6pt8b);
  drawString(8, 44, "RILEVAMENTI TEMPO REALE", LEFT, GxEPD_RED);

  // Card 1: Casa (AHT20)
  display.drawRoundRect(6, 49, 188, 68, 3, GxEPD_BLACK);
  display.drawInvertedBitmap(10, 53, house_thermometer_24x24, 24, 24, GxEPD_BLACK);
  display.setFont(&FONT_6pt8b);
  drawString(38, 64, "CASA (Interno AHT20)", LEFT, GxEPD_BLACK);

  if (bundle.indoor.valid && !std::isnan(bundle.indoor.temp))
  {
    display.setFont(&FONT_16pt8b);
    char inT[16];
    snprintf(inT, sizeof(inT), "%.1f\260C", bundle.indoor.temp);
    drawString(38, 88, inT, LEFT, GxEPD_BLACK);

    display.setFont(&FONT_6pt8b);
    char inH[24];
    snprintf(inH, sizeof(inH), "Umidita': %.0f %%", bundle.indoor.humidity);
    drawString(38, 106, inH, LEFT, GxEPD_BLACK);
  }
  else
  {
    display.setFont(&FONT_11pt8b);
    drawString(38, 85, "--.-\260C (No sensore)", LEFT, GxEPD_BLACK);
  }

  // Card 2: Collina 154m (Campania Live)
  display.drawRoundRect(6, 122, 188, 72, 3, GxEPD_BLACK);
  display.drawInvertedBitmap(10, 126, wi_thermometer_24x24, 24, 24, GxEPD_BLACK);
  display.setFont(&FONT_6pt8b);
  drawString(38, 137, "COLLINA (154m slm)", LEFT, GxEPD_RED);

  if (bundle.hill.valid && !std::isnan(bundle.hill.temp))
  {
    display.setFont(&FONT_16pt8b);
    char hT[16];
    snprintf(hT, sizeof(hT), "%.1f\260C", bundle.hill.temp);
    drawString(38, 161, hT, LEFT, GxEPD_RED);
    drawStationTimestamp(190, 161, bundle.hill, timeInfo);

    display.setFont(&FONT_5pt8b);
    char hSub[40];
    if (std::isnan(bundle.hill.pressure))
      snprintf(hSub, sizeof(hSub), "Umid. %d%% | -- hPa", bundle.hill.humidity);
    else
      snprintf(hSub, sizeof(hSub), "Umid. %d%% | %d hPa",
               bundle.hill.humidity, (int)std::round(bundle.hill.pressure));
    drawString(38, 178, hSub, LEFT, GxEPD_BLACK);

    char hWind[40];
    snprintf(hWind, sizeof(hWind), "Vento: %.1f kts %s",
             bundle.hill.wind_kts, bundle.hill.wind_dir);
    drawString(38, 189, hWind, LEFT, GxEPD_BLACK);
  }
  else
  {
    display.setFont(&FONT_11pt8b);
    drawString(38, 160, "--.-\260C (In attesa)", LEFT, GxEPD_BLACK);
  }

  // Card 3: Porto (Lega Navale)
  display.drawRoundRect(6, 199, 188, 72, 3, GxEPD_BLACK);
  display.drawInvertedBitmap(10, 203, wi_strong_wind_24x24, 24, 24, GxEPD_BLACK);
  display.setFont(&FONT_6pt8b);
  drawString(38, 214, "PORTO (Pontile)", LEFT, GxEPD_BLACK);

  if (bundle.port.valid && !std::isnan(bundle.port.temp))
  {
    display.setFont(&FONT_16pt8b);
    char pT[16];
    snprintf(pT, sizeof(pT), "%.1f\260C", bundle.port.temp);
    drawString(38, 238, pT, LEFT, GxEPD_BLACK);
    drawStationTimestamp(190, 238, bundle.port, timeInfo);

    display.setFont(&FONT_5pt8b);
    char pSub[40];
    snprintf(pSub, sizeof(pSub), "Umid. %d%% | Pioggia: %.1f mm",
             bundle.port.humidity, bundle.port.rain_today);
    drawString(38, 255, pSub, LEFT, GxEPD_BLACK);

    char pWind[40];
    snprintf(pWind, sizeof(pWind), "Vento: %.1f kts (Raff. %.1f)",
             bundle.port.wind_kts, bundle.port.wind_gust_kts);
    drawString(38, 266, pWind, LEFT, GxEPD_BLACK);
  }
  else
  {
    display.setFont(&FONT_11pt8b);
    drawString(38, 237, "--.-\260C (In attesa)", LEFT, GxEPD_BLACK);
  }

  // Vertical Divider
  display.drawLine(198, 38, 198, 273, GxEPD_BLACK);

  // 3. Right Column: Today's Weather Focus (X = 202 to 394)
  display.setFont(&FONT_6pt8b);
  drawString(204, 44, "METEO & PREVISIONE OGGI", LEFT, GxEPD_RED);

  display.drawRoundRect(202, 49, 192, 222, 3, GxEPD_BLACK);

  // Condition icon + text
  display.drawInvertedBitmap(208, 55, wi_day_sunny_48x48, 48, 48, GxEPD_BLACK);
  display.setFont(&FONT_8pt8b);
  drawString(262, 70, bundle.today.valid ? bundle.today.condition : "Oggi", LEFT, GxEPD_BLACK);

  display.setFont(&FONT_6pt8b);
  drawString(262, 85, "Previsione giornata", LEFT, GxEPD_RED);

  display.drawLine(206, 108, 390, 108, GxEPD_BLACK);

  // Escursione termica prevista per oggi
  display.setFont(&FONT_7pt8b);
  drawString(210, 124, "Temperature Previste:", LEFT, GxEPD_BLACK);
  display.setFont(&FONT_9pt8b);
  char tSpan[32];
  if (!std::isnan(bundle.today.temp_min) && !std::isnan(bundle.today.temp_max))
  {
    snprintf(tSpan, sizeof(tSpan), "Min %.1f\260  |  Max %.1f\260",
             bundle.today.temp_min, bundle.today.temp_max);
  }
  else
  {
    snprintf(tSpan, sizeof(tSpan), "Min 14\260  |  Max 21\260");
  }
  drawString(210, 142, tSpan, LEFT, GxEPD_BLACK);

  display.drawLine(206, 150, 390, 150, GxEPD_BLACK);

  // Probabilita pioggia e accumulo
  display.drawInvertedBitmap(208, 154, wi_raindrops_24x24, 24, 24, GxEPD_BLACK);
  display.setFont(&FONT_6pt8b);
  drawString(238, 166, "Probab. Pioggia:", LEFT, GxEPD_BLACK);
  char popBuf[24];
  snprintf(popBuf, sizeof(popBuf), "%.0f %%  (%.1f mm)", bundle.today.pop, bundle.today.rain_mm);
  display.setFont(&FONT_7pt8b);
  drawString(238, 180, popBuf, LEFT, bundle.today.pop > 40.0f ? GxEPD_RED : GxEPD_BLACK);

  // Vento previsto
  display.drawInvertedBitmap(208, 186, wi_strong_wind_24x24, 24, 24, GxEPD_BLACK);
  display.setFont(&FONT_6pt8b);
  drawString(238, 198, "Raffica massima:", LEFT, GxEPD_BLACK);
  char wBuf[24];
  snprintf(wBuf, sizeof(wBuf), "fino a %.1f nodi",
           std::max(bundle.today.wind_max_kts, std::max(bundle.port.wind_gust_kts, bundle.hill.wind_gust_kts)));
  display.setFont(&FONT_7pt8b);
  drawString(238, 212, wBuf, LEFT, GxEPD_BLACK);

  display.drawLine(206, 222, 390, 222, GxEPD_BLACK);

  // Alba / Tramonto
  display.drawInvertedBitmap(208, 226, wi_sunrise_24x24, 24, 24, GxEPD_BLACK);
  display.setFont(&FONT_6pt8b);
  char sunR[16] = "--:--";
  char sunS[16] = "--:--";
  if (bundle.today.sunrise > 0)
  {
    time_t sr = bundle.today.sunrise;
    tm srTm;
    localtime_r(&sr, &srTm);
    snprintf(sunR, sizeof(sunR), "%02d:%02d", srTm.tm_hour, srTm.tm_min);
  }
  if (bundle.today.sunset > 0)
  {
    time_t ss = bundle.today.sunset;
    tm ssTm;
    localtime_r(&ss, &ssTm);
    snprintf(sunS, sizeof(sunS), "%02d:%02d", ssTm.tm_hour, ssTm.tm_min);
  }
  char astroBuf[40];
  snprintf(astroBuf, sizeof(astroBuf), "Alba: %s", sunR);
  drawString(236, 239, astroBuf, LEFT, GxEPD_BLACK);

  display.drawInvertedBitmap(208, 246, wi_sunset_24x24, 24, 24, GxEPD_BLACK);
  snprintf(astroBuf, sizeof(astroBuf), "Tramonto: %s", sunS);
  drawString(236, 259, astroBuf, LEFT, GxEPD_RED);
}

// Draws a 2 px thick line. When dashOn > 0 the line is dashed (dashOn pixels
// drawn, dashOff skipped); `phase` carries the pattern across segments.
static void drawTrendLine(int x0, int y0, int x1, int y1, uint16_t color,
                          int dashOn, int dashOff, int &phase)
{
  const int dx = x1 - x0, dy = y1 - y0;
  const int steps = std::max(std::abs(dx), std::abs(dy));
  for (int k = 0; k <= steps; ++k)
  {
    const int x = steps ? x0 + (dx * k) / steps : x0;
    const int y = steps ? y0 + (dy * k) / steps : y0;
    bool on = true;
    if (dashOn > 0)
    {
      on = (phase % (dashOn + dashOff)) < dashOn;
      if (k < steps) ++phase; // the shared endpoint is counted once
    }
    if (on)
    {
      display.drawPixel(x, y, color);
      display.drawPixel(x, y + 1, color);
    }
  }
}

// Screen 1: Historical Summary (Past 4 Days + Today from SD) & 24h Trend.
// No header or status bar, so the chart gets most of the screen.
void drawScreen1_History(const weather_bundle_t &bundle, const tm &timeInfo)
{
  // 1. Table: past days summary (y = 2 to 107)
  display.drawRect(6, 2, 388, 18, GxEPD_BLACK);
  display.setFont(&FONT_6pt8b);
  drawString(12, 15, "Giorno", LEFT, GxEPD_BLACK);
  drawString(110, 15, "T. Min", CENTER, GxEPD_BLACK);
  drawString(180, 15, "T. Max", CENTER, GxEPD_BLACK);
  drawString(260, 15, "Pioggia", CENTER, GxEPD_BLACK);
  drawString(345, 15, "Raffica Max", CENTER, GxEPD_BLACK);

  int rowY = 20;
  for (int i = 0; i < 5; ++i)
  {
    char dayLabel[24];
    if (i == 0) snprintf(dayLabel, sizeof(dayLabel), "Oggi (%s)", bundle.history[i].date_str);
    else if (i == 1) snprintf(dayLabel, sizeof(dayLabel), "Ieri (%s)", bundle.history[i].date_str);
    else snprintf(dayLabel, sizeof(dayLabel), "%s %s", bundle.history[i].day_name, bundle.history[i].date_str);

    drawString(12, rowY + 12, dayLabel, LEFT, (i == 0) ? GxEPD_RED : GxEPD_BLACK);

    if (bundle.history[i].has_data && !std::isnan(bundle.history[i].temp_min))
    {
      char minBuf[16], maxBuf[16], rainBuf[16], windBuf[16];
      snprintf(minBuf, sizeof(minBuf), "%.1f\260C", bundle.history[i].temp_min);
      snprintf(maxBuf, sizeof(maxBuf), "%.1f\260C", bundle.history[i].temp_max);
      snprintf(rainBuf, sizeof(rainBuf), "%.1f mm", bundle.history[i].rain_total);
      snprintf(windBuf, sizeof(windBuf), "%.0f kts", bundle.history[i].wind_max_kts);

      drawString(110, rowY + 12, minBuf, CENTER, GxEPD_RED);
      drawString(180, rowY + 12, maxBuf, CENTER, GxEPD_BLACK);
      drawString(260, rowY + 12, rainBuf, CENTER, bundle.history[i].rain_total > 0.0f ? GxEPD_RED : GxEPD_BLACK);
      drawString(345, rowY + 12, windBuf, CENTER, GxEPD_BLACK);
    }
    else
    {
      drawString(110, rowY + 12, "--", CENTER, GxEPD_BLACK);
      drawString(180, rowY + 12, "--", CENTER, GxEPD_BLACK);
      drawString(260, rowY + 12, "--", CENTER, GxEPD_BLACK);
      drawString(345, rowY + 12, "--", CENTER, GxEPD_BLACK);
    }

    display.drawLine(6, rowY + 17, 394, rowY + 17, GxEPD_BLACK);
    rowY += 17;
  }

  // 2. Legend (y ~ 120)
  const int legY = 121;
  int lx = 8;
  int phase = 0;
  drawString(lx, legY, "Temp. 24h", LEFT, GxEPD_BLACK);
  lx += 70;
  drawTrendLine(lx, legY - 4, lx + 18, legY - 4, GxEPD_BLACK, 0, 0, phase);
  drawString(lx + 22, legY, "Porto", LEFT, GxEPD_BLACK);
  lx += 64;
  drawTrendLine(lx, legY - 4, lx + 18, legY - 4, GxEPD_RED, 0, 0, phase);
  drawString(lx + 22, legY, "Collina", LEFT, GxEPD_RED);
  lx += 72;
  phase = 0;
  drawTrendLine(lx, legY - 4, lx + 18, legY - 4, GxEPD_BLACK, 4, 3, phase);
  drawString(lx + 22, legY, "Casa", LEFT, GxEPD_BLACK);

  // Sync / offline time, since the status bar is hidden on this page.
  display.setFont(&FONT_5pt8b);
  {
    char syncBuf[32];
    tm syncTm;
    time_t syncEpoch = bundle.last_sync_epoch;
    localtime_r(&syncEpoch, &syncTm);
    snprintf(syncBuf, sizeof(syncBuf), "%s %02d:%02d",
             bundle.is_offline ? "OFFLINE" : "Agg.", syncTm.tm_hour, syncTm.tm_min);
    drawString(DISP_WIDTH - 8, legY, syncBuf, RIGHT,
               bundle.is_offline ? GxEPD_RED : GxEPD_BLACK);
  }

  // 3. 24h trend chart: time on the x axis, last 24 hours ending now.
  const int graphX0 = 32;
  const int graphX1 = 390;
  const int graphY0 = 130;
  const int graphY1 = 282;
  const time_t tEnd   = mktime(const_cast<tm *>(&timeInfo));
  const time_t tStart = tEnd - 24 * 3600;
  // Samples further apart than this are not joined (missed refreshes).
  const time_t MAX_JOIN_GAP = 90 * 60;

  // Range over all three series.
  float vMin = 999.0f, vMax = -999.0f;
  int plotted = 0;
  for (int i = 0; i < bundle.trend_count; ++i)
  {
    const hourly_sample_t &s = bundle.trend_24h[i];
    if (s.timestamp < tStart || s.timestamp > tEnd + 600) continue;
    const float vals[3] = { s.port_temp, s.hill_temp, s.indoor_temp };
    for (float v : vals)
    {
      if (std::isnan(v) || v < -40.0f || v > 60.0f) continue;
      vMin = std::min(vMin, v);
      vMax = std::max(vMax, v);
      ++plotted;
    }
  }
  if (plotted == 0) { vMin = 14.0f; vMax = 24.0f; }

  // Round the range to a "nice" step so there are 4-8 labelled grid lines.
  const float span = vMax - vMin;
  const int step = span <= 5.0f ? 1 : span <= 12.0f ? 2 : span <= 28.0f ? 5 : 10;
  const float gMin = std::floor((vMin - 0.5f) / step) * step;
  float gMax = std::ceil((vMax + 0.5f) / step) * step;
  if (gMax <= gMin) gMax = gMin + step;

  auto xOf = [&](time_t t) {
    if (t < tStart) t = tStart;
    if (t > tEnd) t = tEnd;
    return graphX0 + (int)((double)(t - tStart) * (graphX1 - graphX0) / (double)(tEnd - tStart));
  };
  auto yOf = [&](float v) {
    int y = graphY1 - (int)std::lround((v - gMin) / (gMax - gMin) * (graphY1 - graphY0));
    return std::max(graphY0, std::min(graphY1, y));
  };

  // Horizontal grid + temperature labels
  display.setFont(&FONT_5pt8b);
  for (float v = gMin; v <= gMax + 0.01f; v += step)
  {
    const int y = yOf(v);
    char lbl[12];
    snprintf(lbl, sizeof(lbl), "%.0f\260", v);
    drawString(graphX0 - 4, y + 3, lbl, RIGHT, GxEPD_BLACK);
    if (y != graphY1)
      for (int x = graphX0 + 2; x < graphX1; x += 4)
        display.drawPixel(x, y, GxEPD_BLACK);
  }

  // Vertical grid + hour labels every 3 hours (local time).
  for (time_t t = (tStart / 3600 + 1) * 3600; t <= tEnd; t += 3600)
  {
    tm h;
    localtime_r(&t, &h);
    if (h.tm_hour % 3 != 0) continue;
    const int x = xOf(t);
    for (int y = graphY0; y < graphY1; y += 4)
      display.drawPixel(x, y, GxEPD_BLACK);
    display.drawLine(x, graphY1, x, graphY1 + 3, GxEPD_BLACK);
    char lbl[8];
    snprintf(lbl, sizeof(lbl), "%02d:00", h.tm_hour);
    // keep the last label inside the screen
    const int lblX = std::min(x, DISP_WIDTH - 2 - (int)getStringWidth(lbl) / 2);
    drawString(lblX, DISP_HEIGHT - 6, lbl, CENTER,
               h.tm_hour == 0 ? GxEPD_RED : GxEPD_BLACK);
  }

  // Axes
  display.drawLine(graphX0, graphY1, graphX1, graphY1, GxEPD_BLACK);
  display.drawLine(graphX0, graphY0, graphX0, graphY1, GxEPD_BLACK);

  if (plotted == 0)
  {
    display.setFont(&FONT_7pt8b);
    drawString((graphX0 + graphX1) / 2, (graphY0 + graphY1) / 2,
               "Registrazione trend in corso...", CENTER, GxEPD_BLACK);
    return;
  }

  // Series: 0 = port (black), 1 = hill (red), 2 = indoor (black dashed)
  for (int series = 0; series < 3; ++series)
  {
    const uint16_t color = (series == 1) ? GxEPD_RED : GxEPD_BLACK;
    const int dashOn  = (series == 2) ? 4 : 0;
    const int dashOff = (series == 2) ? 3 : 0;
    int ph = 0;
    int prevIdx = -1;
    for (int i = 0; i < bundle.trend_count; ++i)
    {
      const hourly_sample_t &s = bundle.trend_24h[i];
      const float v = series == 0 ? s.port_temp : series == 1 ? s.hill_temp : s.indoor_temp;
      if (s.timestamp < tStart || s.timestamp > tEnd + 600 ||
          std::isnan(v) || v < -40.0f || v > 60.0f)
        continue;

      const int x = xOf(s.timestamp), y = yOf(v);
      if (prevIdx >= 0 &&
          s.timestamp - bundle.trend_24h[prevIdx].timestamp <= MAX_JOIN_GAP)
      {
        const hourly_sample_t &p = bundle.trend_24h[prevIdx];
        const float pv = series == 0 ? p.port_temp : series == 1 ? p.hill_temp : p.indoor_temp;
        drawTrendLine(xOf(p.timestamp), yOf(pv), x, y, color, dashOn, dashOff, ph);
      }
      else
      {
        display.fillRect(x - 1, y - 1, 3, 3, color); // isolated point
      }
      prevIdx = i;
    }
  }
}

// Screen 2: Nautical & Wind Special (Lega Navale Torre del Greco)
void drawScreen2_Nautical(const weather_bundle_t &bundle, const tm &timeInfo)
{
  // 1. Header
  display.setFont(&FONT_9pt8b);
  drawString(8, 16, "LEGA NAVALE ITALIANA", LEFT, GxEPD_RED);
  display.setFont(&FONT_6pt8b);
  drawString(8, 28, "Sezione Torre del Greco - Stazione Pontile", LEFT, GxEPD_BLACK);
  drawString(DISP_WIDTH - 8, 24, "40\26047'N 14\26021'E", RIGHT, GxEPD_BLACK);
  display.drawLine(6, 33, DISP_WIDTH - 6, 33, GxEPD_BLACK);

  // 2. Left: Graphic Compass Rose (Wind Rose)
  const int cX = 100;
  const int cY = 146;
  const int r  = 54;

  // Circles
  display.drawCircle(cX, cY, r, GxEPD_BLACK);
  display.drawCircle(cX, cY, r - 4, GxEPD_BLACK);
  display.fillCircle(cX, cY, 3, GxEPD_BLACK);

  // Cardinal marks
  display.setFont(&FONT_7pt8b);
  drawString(cX, cY - r - 3, "N", CENTER, GxEPD_RED);
  drawString(cX + r + 6, cY + 4, "E", CENTER, GxEPD_BLACK);
  drawString(cX, cY + r + 11, "S", CENTER, GxEPD_BLACK);
  drawString(cX - r - 6, cY + 4, "W", CENTER, GxEPD_BLACK);

  // Minor ticks (NE, SE, SW, NW)
  float diag = r * 0.7071f;
  display.drawLine(cX + (int)(diag * 0.85f), cY - (int)(diag * 0.85f), cX + (int)diag, cY - (int)diag, GxEPD_BLACK);
  display.drawLine(cX + (int)(diag * 0.85f), cY + (int)(diag * 0.85f), cX + (int)diag, cY + (int)diag, GxEPD_BLACK);
  display.drawLine(cX - (int)(diag * 0.85f), cY + (int)(diag * 0.85f), cX - (int)diag, cY + (int)diag, GxEPD_BLACK);
  display.drawLine(cX - (int)(diag * 0.85f), cY - (int)(diag * 0.85f), cX - (int)diag, cY - (int)diag, GxEPD_BLACK);

  // Wind direction needle
  int deg = bundle.port.wind_deg;
  if (deg == 0 && bundle.hill.wind_deg > 0) deg = bundle.hill.wind_deg;
  float rad = deg * (M_PI / 180.0f);

  int tipX = cX + (int)(sin(rad) * (r - 7));
  int tipY = cY - (int)(cos(rad) * (r - 7));
  int tailX = cX - (int)(sin(rad) * (r / 3));
  int tailY = cY + (int)(cos(rad) * (r / 3));

  // Red pointer arrow
  float perpRad = rad + (M_PI / 2.0f);
  int wA_X = cX + (int)(sin(perpRad) * 6);
  int wA_Y = cY - (int)(cos(perpRad) * 6);
  int wB_X = cX - (int)(sin(perpRad) * 6);
  int wB_Y = cY + (int)(cos(perpRad) * 6);

  display.fillTriangle(tipX, tipY, wA_X, wA_Y, wB_X, wB_Y, GxEPD_RED);
  display.drawLine(cX, cY, tailX, tailY, GxEPD_BLACK);

  // Direction label box below compass
  display.drawRoundRect(20, 222, 160, 44, 3, GxEPD_BLACK);
  display.setFont(&FONT_8pt8b);
  char dirBuf[32];
  snprintf(dirBuf, sizeof(dirBuf), "DIREZ: %s (%d\260)", bundle.port.wind_dir, deg);
  drawString(100, 238, dirBuf, CENTER, GxEPD_RED);

  display.setFont(&FONT_6pt8b);
  char windBeaufort[32];
  snprintf(windBeaufort, sizeof(windBeaufort), "Forza %d Beaufort", meterspersecond_to_beaufort(knots_to_meterspersecond(bundle.port.wind_kts)));
  drawString(100, 256, windBeaufort, CENTER, GxEPD_BLACK);

  // Vertical line
  display.drawLine(198, 38, 198, 273, GxEPD_BLACK);

  // 3. Right: Nautical Indicators (X = 204 to 395)
  // Card 1: Wind speed
  display.drawRoundRect(204, 38, 190, 72, 3, GxEPD_BLACK);
  display.setFont(&FONT_6pt8b);
  drawString(212, 52, "VELOCITA' VENTO", LEFT, GxEPD_BLACK);

  display.setFont(&FONT_24pt8b);
  char ktsBuf[16];
  snprintf(ktsBuf, sizeof(ktsBuf), "%.1f", bundle.port.wind_kts);
  drawString(212, 88, ktsBuf, LEFT, GxEPD_BLACK);

  display.setFont(&FONT_8pt8b);
  drawString(display.getCursorX() + 6, 75, "NODI", LEFT, GxEPD_BLACK);
  display.setFont(&FONT_6pt8b);
  char kmhBuf[16];
  snprintf(kmhBuf, sizeof(kmhBuf), "%.1f km/h", knots_to_kilometersperhour(bundle.port.wind_kts));
  drawString(display.getCursorX() + 6, 92, kmhBuf, LEFT, GxEPD_BLACK);

  // Card 2: Gust (Red)
  display.drawRoundRect(204, 114, 190, 36, 3, GxEPD_BLACK);
  display.setFont(&FONT_7pt8b);
  char gustBuf[32];
  snprintf(gustBuf, sizeof(gustBuf), "RAFFA: %.1f kts", bundle.port.wind_gust_kts);
  drawString(212, 137, gustBuf, LEFT, GxEPD_RED);

  display.setFont(&FONT_6pt8b);
  char gustKmh[24];
  snprintf(gustKmh, sizeof(gustKmh), "(%.1f km/h)", knots_to_kilometersperhour(bundle.port.wind_gust_kts));
  drawString(384, 137, gustKmh, RIGHT, GxEPD_BLACK);

  // Card 3: Other parameters
  display.drawRoundRect(204, 154, 190, 116, 3, GxEPD_BLACK);

  display.drawInvertedBitmap(210, 160, wi_thermometer_16x16, 16, 16, GxEPD_BLACK);
  display.setFont(&FONT_6pt8b);
  char pTBuf[32];
  snprintf(pTBuf, sizeof(pTBuf), "Temp. Pontile: %.1f\260C", bundle.port.temp);
  drawString(232, 172, pTBuf, LEFT, GxEPD_BLACK);

  display.drawInvertedBitmap(210, 178, wi_humidity_16x16, 16, 16, GxEPD_BLACK);
  char pHBuf[32];
  snprintf(pHBuf, sizeof(pHBuf), "Umidita' Pontile: %d %%", bundle.port.humidity);
  drawString(232, 190, pHBuf, LEFT, GxEPD_BLACK);

  display.drawInvertedBitmap(210, 196, wi_barometer_16x16, 16, 16, GxEPD_BLACK);
  char pPBuf[32];
  const float navPress = !std::isnan(bundle.port.pressure) && bundle.port.pressure > 800.0f
                        ? bundle.port.pressure : bundle.hill.pressure;
  if (std::isnan(navPress))
    snprintf(pPBuf, sizeof(pPBuf), "Pressione: -- hPa");
  else
    snprintf(pPBuf, sizeof(pPBuf), "Pressione: %.1f hPa", navPress);
  drawString(232, 208, pPBuf, LEFT, GxEPD_BLACK);

  display.drawInvertedBitmap(210, 214, wi_day_sunny_16x16, 16, 16, GxEPD_BLACK);
  char pSBuf[32];
  snprintf(pSBuf, sizeof(pSBuf), "Radiazione: %.0f W/m\262", bundle.port.solar_wm2);
  drawString(232, 226, pSBuf, LEFT, GxEPD_BLACK);

  display.drawInvertedBitmap(210, 232, air_filter_16x16, 16, 16, GxEPD_BLACK);
  char pUBuf[32];
  snprintf(pUBuf, sizeof(pUBuf), "Indice UV: %d (%s)", bundle.port.uv_index, getUVIdesc(bundle.port.uv_index));
  drawString(232, 244, pUBuf, LEFT, bundle.port.uv_index > 5 ? GxEPD_RED : GxEPD_BLACK);

  display.drawInvertedBitmap(210, 250, wi_raindrops_16x16, 16, 16, GxEPD_BLACK);
  char pRBuf[32];
  snprintf(pRBuf, sizeof(pRBuf), "Pioggia accumulata: %.1f mm", bundle.port.rain_today);
  drawString(232, 262, pRBuf, LEFT, GxEPD_BLACK);
}

// Master rendering function for paged drawing loop
void renderWeatherBundle(const weather_bundle_t &bundle, const tm &timeInfo)
{
  initDisplay();
  do
  {
    if (bundle.current_page == 0)
    {
      drawScreen0_Dashboard(bundle, timeInfo);
    }
    else if (bundle.current_page == 1)
    {
      drawScreen1_History(bundle, timeInfo);
    }
    else
    {
      drawScreen2_Nautical(bundle, timeInfo);
    }

    // The history page uses the full screen for its chart.
    if (bundle.current_page != 1)
    {
      drawPageIndicator(bundle.current_page);
      drawCustomStatusBar(bundle, timeInfo);
    }
  } while (display.nextPage());
  powerOffDisplay();
}


