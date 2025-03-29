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

const GFXfont *defaultFont = &DejaVu18;

LGFX display;
OpenStreetMap osm;
HardwareSerial hws(2);
TinyGPSPlus gps;

int zoom = 15;

double currentLatitude;
double currentLongitude;

bool waitForNewGPSLocation(unsigned long timeoutMs);

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
    STRING
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
    case STRING:
        bar.drawString(result, 0, 0);
        break;
    case SHOW_STATUS:
    {
        char buffer[30];
        snprintf(buffer, sizeof(buffer), "% 4d km/h", static_cast<int>(gps.speed.kmph()));
        bar.drawString(buffer, 0, 0, defaultFont);

        const double homeDistance = haversineDistance(homeLatitude, homeLongitude, currentLatitude, currentLongitude);
        snprintf(buffer, sizeof(buffer), "Home %i km", static_cast<int>(homeDistance) / 1000);
        bar.drawRightString(buffer, bar.width(), 0, defaultFont);

        snprintf(buffer, sizeof(buffer), "S:%li", gps.satellites.value());
        bar.drawCenterString(buffer, 140, 0, defaultFont);

        break;
    }
    }

    bar.pushSprite(0, 0);
    return true;
}

void setup()
{
    Serial.begin(115200);

    hws.begin(GPSBaud, SERIAL_8N1, RXPin, TXPin);

    display.setRotation(0);
    display.setBrightness(110);
    display.begin();

    String result = "Connecting WiFi"; // Use the result String as input
    showStatusBar(STRING, result);

    WiFi.setSleep(false);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
        delay(5);

    vTaskPrioritySet(NULL, 9);

    result = "Waiting for location";
    showStatusBar(STRING, result);

    osm.setSize(display.width(), display.height() - defaultFont->yAdvance);
    osm.resizeTilesCache(20);
}

void loop()
{
    constexpr unsigned long gpsTimeoutThreshold = 2000;
    static unsigned long lastGpsUpdate = millis();
    if (!waitForNewGPSLocation(10))
    {
        if (millis() - lastGpsUpdate > gpsTimeoutThreshold)
        {
            String result = "GPS Wiring or Antenna Error";
            showStatusBar(STRING, result);
            lastGpsUpdate = millis();
        }
        return;
    }
    lastGpsUpdate = millis(); // Reset timeout tracker

    currentLatitude = gps.location.lat();
    currentLongitude = gps.location.lng();

    String result;
    showStatusBar(SHOW_STATUS, result);

    static unsigned long initTimeMs = millis();
    static unsigned long lastUpdateMs = 0;
    if (millis() - lastUpdateMs > 750)
    {
        static LGFX_Sprite map(&display);
        const bool success = osm.fetchMap(map, currentLongitude, currentLatitude, zoom);
        if (success)
        {
            map.drawCircle(map.width() / 2, map.height() / 2, 10, TFT_DARKCYAN);
            map.drawCircle(map.width() / 2, map.height() / 2, 6, TFT_DARKCYAN);
            map.drawCircle(map.width() / 2, map.height() / 2, 3, TFT_BLACK);

            if (millis() - initTimeMs < 4000)
            {
                map.drawCenterString("ESP32 OSM tracker", map.width() / 2, 30, defaultFont);
                map.drawCenterString("0.99.2", map.width() / 2, 70, defaultFont);
            }

            map.pushSprite(0, defaultFont->yAdvance);
        }
        lastUpdateMs = millis();
    }
}

bool waitForNewGPSLocation(unsigned long timeoutMs)
{
    constexpr uint32_t STALE_TIME = 500;
    unsigned long startTime = millis();

    while (true)
    {
        while (hws.available() > 0)
        {
            if (gps.encode(hws.read()))
            {
                if (gps.location.isValid() && gps.location.age() < STALE_TIME)
                    return true;
            }
        }

        if (millis() - startTime > timeoutMs)
            return false;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}