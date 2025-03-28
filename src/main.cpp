#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include <OpenStreetMap-esp32.h>

#include <LGFX_AUTODETECT.hpp>
#include <LovyanGFX.hpp>
#include <TinyGPS++.h>

#include "secrets.h"

void waitForNewGPSLocation();

static const int RXPin = 13, TXPin = 14;
static const uint32_t GPSBaud = 38400;

LGFX display;
OpenStreetMap osm;
HardwareSerial hws(2);
TinyGPSPlus gps;

int zoom = 15;

void setup()
{
    Serial.begin(115200);

    hws.begin(GPSBaud, SERIAL_8N1, RXPin, TXPin);

    display.setRotation(0);
    display.setBrightness(110);
    display.begin();

    display.drawString("Connecting WiFi", 0, 0, &DejaVu12);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(10);
        Serial.print(".");
    }

    vTaskPrioritySet(NULL, 9);

    display.drawString("Waiting for location", 0, 15, &DejaVu12);

    waitForNewGPSLocation();

    log_i("longitude: %f", gps.location.lng());
    log_i("latitude: %f", gps.location.lat());

    display.drawString("Fetching osm tiles...", 0, 30, &DejaVu12);

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

void displayInfo()
{
    Serial.print(F("Location: "));
    if (gps.location.isValid())
    {
        Serial.print(gps.location.lat(), 6);
        Serial.print(F(","));
        Serial.print(gps.location.lng(), 6);
        Serial.println();
    }
    else
    {
        Serial.print(F("INVALID"));
    }
}

void loop()
{
    waitForNewGPSLocation();

    double newLat = gps.location.lat();
    double newLng = gps.location.lng();

    static LGFX_Sprite map(&display);
    const bool success = osm.fetchMap(map, newLng, newLat, zoom);
    if (success)
    {
        // draw a circle marking the location
        map.drawCircle(map.width() / 2, map.height() / 2, 10, TFT_DARKCYAN);
        map.drawCircle(map.width() / 2, map.height() / 2, 6, TFT_DARKCYAN);
        map.drawCircle(map.width() / 2, map.height() / 2, 3, TFT_DARKCYAN);

        char buffer[30];
        snprintf(buffer, sizeof(buffer), " %d km/h ", static_cast<int>(gps.speed.kmph()));
        map.drawString(buffer, 0, 0, &DejaVu18);

        //calulate the distance to home
        const double homeDistance =  haversineDistance(homeLatitude, homeLongitude, newLat, newLng);
        snprintf(buffer, sizeof(buffer), " Home: %i km ", static_cast<int>(homeDistance / 1000));
        map.drawRightString(buffer, map.width(), 0, &DejaVu18);

        map.pushSprite(0, 0);
    }
}

void waitForNewGPSLocation()
{
    while (true)
    {
        while (hws.available())
            gps.encode(hws.read());

        if (gps.location.isUpdated() && gps.location.isValid())
            break;

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
