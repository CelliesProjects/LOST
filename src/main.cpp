#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>

#include <LGFX_AUTODETECT.hpp>
#include <LovyanGFX.hpp>
#include <OpenStreetMap-esp32.h>
#include <TinyGPS++.h>

#include "secrets.h"
#include "NetworkDetails.h"

static const int RXPin = 13, TXPin = 14;
static const uint32_t GPSBaud = 38400;
static const GFXfont *statusBarFont = &DejaVu18;

HardwareSerial hws(2);
LGFX display;
OpenStreetMap osm;
TinyGPSPlus gps;

enum statusBarType
{
    SHOW_STATUS,
    SHOW_STRING,
    SHOW_CLOCK
};

static int zoom = 15;
static LGFX_Sprite currentMap(&display);
static statusBarType currentBarType = SHOW_STATUS;
static bool sdIsMounted = false;
static bool isRecording = false;

bool connectToNetwork(String &ssid)
{
    for (const auto &net : knownNetworks)
    {
        if (ssid == net.ssid)
        {
            WiFi.begin(net.ssid, net.password);
            log_i("Connecting to %s...", net.ssid);

            unsigned long startAttemptTime = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000)
                vTaskDelay(pdMS_TO_TICKS(5));

            if (WiFi.status() == WL_CONNECTED)
            {
                log_i("Connected! IP: %s", WiFi.localIP().toString().c_str());
                return true;
            }
            log_i("Connection failed.");
            return false;
        }
    }
    log_i("Selected network is not in the known list.");
    return false;
}

void drawNetworkList(std::vector<String> &networks)
{
    display.fillScreen(TFT_BLACK);
    display.drawCenterString("Select Network:", display.width() / 2, 0, &DejaVu18);

    int rectHeight = 40;
    int spacing = 5;
    int startY = 30;

    for (size_t i = 0; i < networks.size(); i++)
    {
        int yPos = startY + i * (rectHeight + spacing);
        display.drawRect(0, yPos, display.width(), rectHeight, TFT_WHITE);
        display.setTextColor(TFT_WHITE);
        display.setTextDatum(middle_centre);
        display.drawString(networks[i], display.width() / 2, yPos + (rectHeight / 2), &DejaVu24);
    }
}

void selectNetworkFromList(std::vector<String> &networks, int32_t &network)
{
    uint16_t x, y;
    if (display.getTouch(&x, &y))
    {
        int rectHeight = 40;
        int spacing = 5;
        int startY = 30;

        for (size_t i = 0; i < networks.size(); i++)
        {
            const int yPos = startY + i * (rectHeight + spacing);
            if (y > yPos && y < yPos + rectHeight)
            {
                display.fillRect(0, yPos, display.width(), rectHeight, TFT_DARKCYAN);
                display.setTextColor(TFT_WHITE, TFT_DARKCYAN);
                display.setTextDatum(middle_centre);
                display.drawString(networks[i], display.width() / 2, yPos + (rectHeight / 2), &DejaVu24);
                network = i;
                connectToNetwork(networks[i]);
                return;
            }
        }
    }
}

bool isKnownNetwork(const String &ssid)
{
    for (const auto &net : knownNetworks)
    {
        if (ssid == net.ssid)
            return true;
    }
    return false;
}

void selectNetwork()
{
    std::vector<String> networks;
    int numNetworks = WiFi.scanNetworks();
    for (int i = 0; i < numNetworks; i++)
    {
        String ssid = WiFi.SSID(i);
        if (isKnownNetwork(ssid))
            networks.push_back(ssid);
    }

    if (networks.empty())
    {
        display.fillScreen(TFT_BLACK);
        display.drawCenterString("No Known Networks", display.width() / 2, display.height() / 2, &DejaVu40);
        while (1)
            delay(100);
    }

    if (networks.size() == 1)
    {
        connectToNetwork(networks[0]);
        // TODO: check if really connected
        return;
    }

    drawNetworkList(networks);

    int32_t selectedNetwork = -1;
    while (selectedNetwork == -1)
        selectNetworkFromList(networks, selectedNetwork);

    // TODO: check if really connected
}

void drawMap(LGFX_Sprite &map)
{
    if (!map.getBuffer())
        return;
    map.pushSprite(0, statusBarFont->yAdvance);
}

bool waitForNewGPSLocation(unsigned long timeoutMs)
{
    constexpr uint32_t STALETIME_MS = 5;
    const unsigned long startTime = millis();
    while (true)
    {
        while (hws.available() > 0)
            if (gps.encode(hws.read()) && gps.location.isValid() && gps.location.age() < STALETIME_MS)
                return true;

        if (timeoutMs && millis() - startTime > timeoutMs)
            return false;

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

bool showStatusBar(statusBarType type, String &result)
{
    static LGFX_Sprite bar(&display);

    if (bar.width() != display.width())
    {
        bar.setPsram(true);
        bar.deleteSprite();
        bar.setFont(statusBarFont);
        bar.createSprite(display.width(), statusBarFont->yAdvance);
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
        bar.drawString(buffer, 0, 0);
        snprintf(buffer, sizeof(buffer), "S:%li", gps.satellites.value());
        bar.drawCenterString(buffer, 130, 0);
        const float homeDistance = gps.distanceBetween(homeLatitude, homeLongitude, gps.location.lat(), gps.location.lng());
        snprintf(buffer, sizeof(buffer), "Home %i km", static_cast<int>(homeDistance) / 1000);
        bar.drawRightString(buffer, bar.width(), 0);

        break;
    }
    case SHOW_CLOCK:
    {
        time_t now = time(NULL);
        struct tm localTime;
        localtime_r(&now, &localTime);

        char timeBuffer[16];
        snprintf(timeBuffer, sizeof(timeBuffer), "%02i:%02i:%02i", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);

        bar.drawCenterString(timeBuffer, bar.width() / 2, 0);
        break;
    }
    default:
        log_w("unhandled status bar type");
    }

    bar.pushSprite(0, 0);
    return true;
}

void drawFreshMap(double longitude, double latitude, uint8_t zoom)
{
    if (!osm.fetchMap(currentMap, longitude, latitude, zoom))
    {
        String error = "Failed to fetch map";
        showStatusBar(SHOW_STRING, error); // will be barely visible before overwritten
        log_e("%s", error.c_str());
        return;
    }

    currentMap.drawCircle(currentMap.width() / 2, currentMap.height() / 2, 10, TFT_DARKCYAN);
    currentMap.drawCircle(currentMap.width() / 2, currentMap.height() / 2, 6, TFT_DARKCYAN);
    currentMap.drawCircle(currentMap.width() / 2, currentMap.height() / 2, 3, TFT_BLACK);

    static unsigned long initTimeMs = millis();
    if (millis() - initTimeMs < 4000)
    {
        currentMap.setTextColor(TFT_BLACK);
        currentMap.drawCenterString("LOST", currentMap.width() / 2, 30, &DejaVu72);
        currentMap.drawCenterString("LGFX OSM TinyGPS", currentMap.width() / 2, 110, statusBarFont);
        currentMap.drawCenterString(GIT_VERSION, currentMap.width() / 2, 140, statusBarFont);
    }
    currentMap.pushSprite(0, statusBarFont->yAdvance);
}

void setup()
{
    Serial.begin(115200);

    hws.begin(GPSBaud, SERIAL_8N1, RXPin, TXPin);

    sdIsMounted = SD.begin(SDCARD_SS);

    log_i("SD card %s", sdIsMounted ? "mounted" : "failed");

    display.setRotation(0);
    display.setBrightness(110);
    display.begin();

    String str = "Scanning for WiFi networks";
    showStatusBar(SHOW_STRING, str);

    WiFi.mode(WIFI_STA);
    selectNetwork();
    configTzTime(TIMEZONE, NTP_POOL);

    vTaskPrioritySet(NULL, 9);

    osm.setSize(display.width(), display.height() - statusBarFont->yAdvance);
    osm.resizeTilesCache(20);

    str = "Waiting for location";
    showStatusBar(SHOW_STRING, str);
}

constexpr int32_t MENU_HEIGHT = 120;
constexpr int32_t BUTTON_WIDTH = 106;
constexpr int32_t BUTTON_START_Y = 200;
constexpr int32_t CONFIRM_DURATION_MS = 600;
constexpr uint16_t PROGRESS_COLOR = TFT_DARKGREEN;
constexpr int32_t BUTTON_X[] = {0, 107, 214};
constexpr uint16_t BUTTON_COLORS[] = {TFT_RED, TFT_GREEN, TFT_BLUE};

bool confirm(LGFX_Device &dest, int32_t buttonIndex)
{
    int32_t buttonX = BUTTON_X[buttonIndex];
    uint16_t x, y;
    uint32_t startTime = millis();

    int32_t textX = buttonX + (BUTTON_WIDTH / 2);
    int32_t textY = (dest.height() - MENU_HEIGHT) + (MENU_HEIGHT / 2);

    while (millis() - startTime < CONFIRM_DURATION_MS)
    {
        if (!dest.getTouch(&x, &y))
        {
            constexpr char *menu[] = {"Cancel", "Cancel", "Cancel"};
            dest.fillRect(buttonX, dest.height() - MENU_HEIGHT, BUTTON_WIDTH, MENU_HEIGHT, TFT_WHITE);
            dest.setTextColor(TFT_BLACK, TFT_WHITE);
            dest.drawString(menu[buttonIndex], textX, textY, &DejaVu24);
            return false;
        }

        int32_t progressHeight = (millis() - startTime) * MENU_HEIGHT / CONFIRM_DURATION_MS;
        dest.fillRect(buttonX, dest.height() - progressHeight, BUTTON_WIDTH, 2, PROGRESS_COLOR);

        if (currentBarType != SHOW_STRING)
        {
            String stub;
            showStatusBar(currentBarType, stub);
        }
    }

    return true;
}

bool handleTouchScreen(LGFX_Device &dest)
{
    uint16_t x, y;
    if (!dest.getTouch(&x, &y) || (y <= BUTTON_START_Y && y >= statusBarFont->yAdvance))
        return false;

    if (y < statusBarFont->yAdvance)
    {
        currentBarType = (currentBarType == SHOW_CLOCK) ? SHOW_STATUS : SHOW_CLOCK;
        String str;
        showStatusBar(currentBarType, str);
        vTaskDelay(pdMS_TO_TICKS(40));
        return false;
    }

    uint8_t buttonIndex = (x < BUTTON_X[1]) ? 0 : (x < BUTTON_X[2]) ? 1
                                                                    : 2;
    int32_t buttonX = BUTTON_X[buttonIndex];
    uint16_t color = BUTTON_COLORS[buttonIndex];

    dest.fillRect(buttonX, dest.height() - MENU_HEIGHT, BUTTON_WIDTH, MENU_HEIGHT, color);

    int32_t textX = buttonX + (BUTTON_WIDTH / 2);
    int32_t textY = (dest.height() - MENU_HEIGHT) + (MENU_HEIGHT / 2);

    dest.setTextDatum(textdatum_t::middle_center);
    dest.setTextColor(TFT_BLACK, BUTTON_COLORS[buttonIndex]);

    // determine how to handle the start and stop button
    // if (!sdIsMounted)
    //    sdIsMounted = SD.begin(SDCARD_SS);
    char startBtnTxt[10];
    snprintf(startBtnTxt, sizeof(startBtnTxt), "%s", sdIsMounted ? "Start" : "NO SD");

    const char *line1[] = {startBtnTxt, "Set", startBtnTxt};
    const char *line2[] = {"", "Home", ""};
    dest.drawString(line1[buttonIndex], textX, textY - 20, &DejaVu24);
    dest.drawString(line2[buttonIndex], textX, textY + 20, &DejaVu24);

    if (!confirm(display, buttonIndex))
    {
        vTaskDelay(pdMS_TO_TICKS(300));
        return true;
    }

    dest.fillRect(buttonX, dest.height() - MENU_HEIGHT, BUTTON_WIDTH, MENU_HEIGHT, TFT_WHITE);
    dest.setTextColor(TFT_BLACK, TFT_WHITE);

    switch (buttonIndex)
    {
    case 0:
        dest.drawString("Stub!", textX, textY, &DejaVu24);
        break;
    case 1:
        dest.fillRect(buttonX, dest.height() - MENU_HEIGHT, BUTTON_WIDTH, MENU_HEIGHT, TFT_WHITE);
        dest.drawString("Saving", textX, textY, &DejaVu24);
        // beep();
        homeLatitude = gps.location.lat();
        homeLongitude = gps.location.lng();
        vTaskDelay(pdMS_TO_TICKS(160));
        dest.setTextColor(TFT_BLACK, TFT_GREEN);
        dest.fillRect(buttonX, dest.height() - MENU_HEIGHT, BUTTON_WIDTH, MENU_HEIGHT, TFT_GREEN);
        dest.drawString("Home", textX, textY - 20, &DejaVu24);
        dest.drawString("Set", textX, textY + 20, &DejaVu24);
        break;
    case 2:
        dest.drawString("Stub!", textX, textY, &DejaVu24);
        break;
    default:
        log_e("out of range button %i pressed?", buttonIndex);
        break;
    }
    String dummy;
    showStatusBar(currentBarType, dummy);
    vTaskDelay(pdMS_TO_TICKS(800));
    return true;
}

void loop()
{
    if (handleTouchScreen(display))
        drawMap(currentMap);

    constexpr unsigned long gpsTimeoutThreshold = 3000;
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
    showStatusBar(currentBarType, str);

    static unsigned long lastUpdateMs = 0;
    if (millis() - lastUpdateMs > 500)
    {
        drawFreshMap(gps.location.lng(), gps.location.lat(), zoom);
        lastUpdateMs = millis();
    }
}
