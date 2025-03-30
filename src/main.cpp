#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include <OpenStreetMap-esp32.h>

#include <LGFX_AUTODETECT.hpp>
#include <LovyanGFX.hpp>
#include <TinyGPS++.h>

#include "secrets.h"

static const int RXPin = 13, TXPin = 14;
static const uint32_t GPSBaud = 38400;
static const GFXfont *defaultFont = &DejaVu18;

HardwareSerial hws(2);
LGFX display;
OpenStreetMap osm;
TinyGPSPlus gps;

int zoom = 15;
double currentLatitude;
double currentLongitude;

bool waitForNewGPSLocation(unsigned long timeoutMs)
{
    constexpr uint32_t STALETIME_MS = 5;
    const unsigned long startTime = millis();
    while (true)
    {
        while (hws.available() > 0)
            if (gps.encode(hws.read()) && gps.location.isValid() && gps.location.age() < STALETIME_MS)
                return true;

        if (millis() - startTime > timeoutMs)
            return false;

        vTaskDelay(pdMS_TO_TICKS(STALETIME_MS));
    }
}

double haversineDistance(double lat1, double lng1, double lat2, double lng2)
{
    constexpr double earthRadius = 6371000; // Earth radius in meters
    double dLat = (lat2 - lat1) * PI / 180.0;
    double dLng = (lng2 - lng1) * PI / 180.0;

    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(lat1 * PI / 180.0) * cos(lat2 * PI / 180.0) *
                   sin(dLng / 2) * sin(dLng / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));

    return earthRadius * c;
}

enum statusBarType
{
    SHOW_STATUS,
    SHOW_STRING
};

bool showStatusBar(statusBarType type, String &result)
{
    static LGFX_Sprite bar(&display);

    if (bar.width() != display.width())
    {
        bar.setPsram(true);
        bar.deleteSprite();
        bar.setFont(defaultFont);
        bar.createSprite(display.width(), defaultFont->yAdvance);
        if (!bar.getBuffer())
        {
            result = "could not allocate statusbar";
            return false;
        }
    }

    bar.clear();

    switch (type)
    {
    case SHOW_STRING:
        bar.drawCenterString(result, bar.width() / 2, 0);
        break;
    case SHOW_STATUS:
    {
        char buffer[30];
        snprintf(buffer, sizeof(buffer), "%3d km/h", static_cast<int>(gps.speed.kmph()));
        bar.drawString(buffer, 0, 0, defaultFont);

        snprintf(buffer, sizeof(buffer), "S:%li", gps.satellites.value());
        bar.drawCenterString(buffer, 130, 0, defaultFont);

        const double homeDistance = haversineDistance(homeLatitude, homeLongitude, currentLatitude, currentLongitude);
        snprintf(buffer, sizeof(buffer), "Home %i km", static_cast<int>(homeDistance) / 1000);
        bar.drawRightString(buffer, bar.width(), 0, defaultFont);

        break;
    }
    default:
        log_w("unhandled status bar type");
    }

    bar.pushSprite(0, 0);
    return true;
}

void drawMap(double longitude, double latitude, uint8_t zoom)
{
    static unsigned long initTimeMs = millis();
    static LGFX_Sprite map(&display);
    const bool success = osm.fetchMap(map, longitude, latitude, zoom);
    if (success)
    {
        map.drawCircle(map.width() / 2, map.height() / 2, 10, TFT_DARKCYAN);
        map.drawCircle(map.width() / 2, map.height() / 2, 6, TFT_DARKCYAN);
        map.drawCircle(map.width() / 2, map.height() / 2, 3, TFT_BLACK);

        if (millis() - initTimeMs < 4000)
        {
            map.drawCenterString("OSM tracker", map.width() / 2, 30, defaultFont);
            map.drawCenterString("0.99.2", map.width() / 2, 70, defaultFont);
        }

        map.pushSprite(0, defaultFont->yAdvance);
    }
    else
    {
        String error = "Failed to fetch map";
        showStatusBar(SHOW_STRING, error); // will be barely visible before overwritten
        log_e("%s", error.c_str());
    }
}

void setup()
{
    Serial.begin(115200);

    hws.begin(GPSBaud, SERIAL_8N1, RXPin, TXPin);

    display.setRotation(0);
    display.setBrightness(110);
    display.begin();

    String str = "Connecting WiFi";
    showStatusBar(SHOW_STRING, str);

    WiFi.setSleep(false);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
        delay(5);

    vTaskPrioritySet(NULL, 9);

    osm.setSize(display.width(), display.height() - defaultFont->yAdvance);
    osm.resizeTilesCache(20);

    str = "Waiting for location";
    showStatusBar(SHOW_STRING, str);
}

bool checkButtons(LGFX_Device &dest)
{

    constexpr int32_t MENU_HEIGHT = 40;
    constexpr int32_t BUTTON_START_Y = 200;
    constexpr int32_t BUTTON_WIDTH = 106;

    constexpr int32_t BUTTON_X[] = {0, 107, 214};
    constexpr uint16_t BUTTON_COLORS[] = {TFT_RED, TFT_GREEN, TFT_BLUE};

    uint16_t x, y;
    if (!dest.getTouch(&x, &y) || y <= BUTTON_START_Y)
        return false;

    uint8_t buttonIndex = (x < BUTTON_X[1]) ? 0 : (x < BUTTON_X[2]) ? 1
                                                                    : 2;
    int32_t buttonX = BUTTON_X[buttonIndex];
    uint16_t color = BUTTON_COLORS[buttonIndex];

    dest.fillRect(buttonX, dest.height() - MENU_HEIGHT, BUTTON_WIDTH, MENU_HEIGHT, color);

    int32_t textX = buttonX + (BUTTON_WIDTH / 2);
    int32_t textY = (dest.height() - MENU_HEIGHT) + (MENU_HEIGHT / 2);

    const char *menu[] = {"Start", "Home", "Stop"};
    dest.setTextDatum(textdatum_t::middle_center);
    dest.setTextColor(TFT_BLACK, BUTTON_COLORS[buttonIndex]);
    dest.drawString(menu[buttonIndex], textX, textY, &DejaVu24);

    switch (buttonIndex)
    {
    case 0:
        break;
    case 1:
        // Show a dialog before committing to the update
        if (gps.location.isValid())
        {
            homeLatitude = gps.location.lat();
            homeLongitude = gps.location.lng();
        }
        else
            log_w("GPS location not valid. Update skipped.");
        break;
    case 2:
        break;
    default:
        log_e("out of range button %i pressed", buttonIndex);
        break;
    }

    delay(buttonIndex == 1 ? 50 : 300);

    return true;
}

void loop()
{
    if (checkButtons(display))
        drawMap(currentLongitude, currentLatitude, zoom);

    constexpr unsigned long gpsTimeoutThreshold = 2000;
    static unsigned long lastGpsUpdate = millis();
    if (!waitForNewGPSLocation(10))
    {
        if (millis() - lastGpsUpdate > gpsTimeoutThreshold)
        {
            String result = "GPS Wiring or Antenna Error";
            showStatusBar(SHOW_STRING, result);
            lastGpsUpdate = millis();
        }
        return;
    }
    lastGpsUpdate = millis();

    String str;
    showStatusBar(SHOW_STATUS, str);

    currentLatitude = gps.location.lat();
    currentLongitude = gps.location.lng();

    static unsigned long lastUpdateMs = 0;
    if (millis() - lastUpdateMs > 750)
    {
        drawMap(currentLongitude, currentLatitude, zoom);
        lastUpdateMs = millis();
    }
}
