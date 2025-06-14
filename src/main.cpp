#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>

#include <LGFX_AUTODETECT.hpp>
#include <LovyanGFX.hpp>
#include <OpenStreetMap-esp32.hpp>
#include <TinyGPS++.h>
#include <PNGdec.h>

#include "secrets.h"

static constexpr int8_t RXPin = 13, TXPin = 14;
static constexpr uint32_t GPSBaud = 38400;
static constexpr GFXfont const *statusBarFont = &DejaVu18;
static constexpr uint16_t MAP_REC_OFFSET = 25;
static constexpr uint16_t MAP_REC_SIZE = 12;
static constexpr uint16_t SAVE_INTERVAL_MS = 500;

HardwareSerial hws(2);
LGFX display;
OpenStreetMap osm;
TinyGPSPlus gps;
File logFile;

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

bool showStatusBar(statusBarType type, String &result);

bool connectToNetwork(String &ssid)
{
    for (const auto &net : knownNetworks)
    {
        if (ssid == net.ssid)
        {
            WiFi.setSleep(false);
            WiFi.begin(net.ssid, net.password);
            log_i("Connecting to %s...", net.ssid);

            display.fillScreen(TFT_BLACK);
            display.setTextColor(TFT_WHITE, TFT_BLACK);
            display.drawCenterString("Connecting to", display.width() / 2, (display.height() / 2) - 15, &DejaVu18);
            display.drawCenterString(net.ssid, display.width() / 2, (display.height() / 2) + 15, &DejaVu18);

            unsigned long startMs = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - startMs < 10000)
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

void connectToNetworkFromList(std::vector<String> &networks, int32_t &network)
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

void scanForKnownNetWorks(std::vector<String> &networks)
{
    while (true)
    {
        display.fillScreen(TFT_BLACK);

        String str = "Scanning WiFi networks...";
        showStatusBar(SHOW_STRING, str);

        int numNetworks = WiFi.scanNetworks();
        for (int i = 0; i < numNetworks; i++)
        {
            String ssid = WiFi.SSID(i);
            if (isKnownNetwork(ssid))
                networks.push_back(ssid);
        }
        if (networks.size())
            break;

        display.fillScreen(TFT_BLACK);
        display.drawCenterString("No known networks", display.width() / 2, (display.height() / 2 - 15), &DejaVu18);
        display.drawCenterString("Tap the screen to scan again", display.width() / 2, (display.height() / 2) + 15, &DejaVu18);

        uint16_t x, y;
        while (!display.getTouch(&x, &y))
            vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void selectNetwork()
{
    while (true)
    {
        std::vector<String> networks;
        scanForKnownNetWorks(networks);
        if (networks.size() == 1)
            connectToNetwork(networks[0]);
        else
        {
            drawNetworkList(networks);
            int32_t selectedNetwork = -1;
            while (selectedNetwork == -1)
                connectToNetworkFromList(networks, selectedNetwork);
        }
        if (WiFi.isConnected())
            break;

        display.fillScreen(TFT_BLACK);
        display.drawCenterString("Could not connect", display.width() / 2, (display.height() / 2) - 15, &DejaVu18);
        display.drawCenterString("Tap the screen to scan again", display.width() / 2, (display.height() / 2) + 15, &DejaVu18);

        uint16_t x, y;
        while (!display.getTouch(&x, &y))
            vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void drawMap(LGFX_Sprite &map)
{
    if (map.getBuffer())
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
        if (homeLatitude || homeLongitude)
        {
            const float homeDistance = gps.distanceBetween(homeLatitude, homeLongitude, gps.location.lat(), gps.location.lng());
            snprintf(buffer, sizeof(buffer), "Home %i km", static_cast<int>(homeDistance) / 1000);
            bar.drawRightString(buffer, bar.width(), 0);
        }
        break;
    }
    case SHOW_CLOCK:
    {
        time_t now = time(NULL);
        struct tm localTime;
        localtime_r(&now, &localTime);
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%02i:%02i:%02i", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
        bar.drawCenterString(buffer, bar.width() / 2, 0);
        break;
    }
    default:
        log_w("unhandled status bar type");
    }

    bar.pushSprite(0, 0);
    return true;
}

void showProgramName(int32_t midX)
{
    constexpr char *PROGRAM_NAME = "LOST";
    constexpr char *LOST = "LGFX OSM TinyGPS";
    constexpr char *AND_PNGDEC = "PNGdec";

    currentMap.setTextColor(TFT_BLACK);
    currentMap.drawCenterString(PROGRAM_NAME, midX - 1, 30, &DejaVu72);
    currentMap.drawCenterString(PROGRAM_NAME, midX + 1, 30, &DejaVu72);
    currentMap.drawCenterString(LOST, midX, 110, &DejaVu24);
    currentMap.drawCenterString(LOST, midX - 1, 110, &DejaVu24);
    currentMap.drawCenterString(AND_PNGDEC, midX, 140, &DejaVu24);
    currentMap.drawCenterString(AND_PNGDEC, midX - 1, 140, &DejaVu24);
    currentMap.drawCenterString(GIT_VERSION, midX, 170, &DejaVu18);
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

    if (isRecording)
    {
        currentMap.fillCircle(MAP_REC_OFFSET, MAP_REC_OFFSET, MAP_REC_SIZE, TFT_RED);
        currentMap.setTextColor(TFT_BLACK);
        currentMap.setTextDatum(middle_right);
        currentMap.drawString(logFile.name(), currentMap.width() - MAP_REC_OFFSET, MAP_REC_OFFSET, &DejaVu24);
    }

    const int32_t midX = currentMap.width() / 2;
    const int32_t midY = currentMap.height() / 2;

    static unsigned long initTimeMs = millis();
    if (millis() - initTimeMs < 5000)
        showProgramName(midX);
    else
    {
        currentMap.drawCircle(midX, midY, 10, TFT_BLACK);
        currentMap.drawCircle(midX, midY, 6, TFT_BLACK);
        currentMap.drawCircle(midX, midY, 3, TFT_BLACK);
    }

    currentMap.pushSprite(0, statusBarFont->yAdvance);
}

void setup()
{
    Serial.begin(115200);
    hws.begin(GPSBaud, SERIAL_8N1, RXPin, TXPin);
    display.setRotation(0);
    display.setBrightness(110);
    display.begin();

    sdIsMounted = SD.begin(SDCARD_SS);

    WiFi.mode(WIFI_STA);
    selectNetwork();
    configTzTime(TIME_ZONE, NTP_POOL);

    osm.setRenderMode(RenderMode::FAST);
    osm.setSize(display.width(), display.height() - statusBarFont->yAdvance);
    // osm.resizeTilesCache(20);

    display.fillScreen(TFT_BLACK);
    display.drawCenterString("Waiting for GPS signal", display.width() / 2, (display.height() / 2 - 15), &DejaVu18);
    display.drawCenterString("May take up to 3 minutes", display.width() / 2, (display.height() / 2) + 15, &DejaVu18);
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

void logGPSData()
{
    if (!gps.location.isValid() || !gps.altitude.isValid() || !gps.time.isValid())
        return;

    // Format: TIME,LAT,LON,ALT
    char line[64];
    snprintf(line, sizeof(line), "%04d-%02d-%02dT%02d:%02d:%02dZ,%.6f,%.6f,%.1f\n",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second(),
             gps.location.lat(), gps.location.lng(), gps.altitude.meters());

    logFile.print(line);
    logFile.flush();

    log_v("location to %s , %lu bytes total", logFile.name(), logFile.size());
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

    dest.setTextDatum(middle_center);
    dest.setTextColor(TFT_BLACK, color);

    const char *buttonTexts[3][2] = {
        {sdIsMounted ? (isRecording ? "REC" : "Start") : "NO SD", "Log"},
        {"Save", "Home"},
        {isRecording ? "STOP" : "", ""}};

    static constexpr GFXfont const *font = &DejaVu24;
    dest.drawString(buttonTexts[buttonIndex][0], textX, textY - font->yAdvance, font);
    dest.drawString(buttonTexts[buttonIndex][1], textX, textY, font);

    if (!confirm(display, buttonIndex))
    {
        vTaskDelay(pdMS_TO_TICKS(300)); // change so that -the clock- statusbar is updated
        return true;
    }

    dest.fillRect(buttonX, dest.height() - MENU_HEIGHT, BUTTON_WIDTH, MENU_HEIGHT, TFT_WHITE);
    dest.setTextColor(TFT_BLACK, TFT_WHITE);

    static char filename[32];
    switch (buttonIndex)
    {
    case 0:
        if (sdIsMounted && !isRecording)
        {
            int fileIndex = 1;
            do
            {
                snprintf(filename, sizeof(filename), "/gps_track%d.txt", fileIndex);
                fileIndex++;
            } while (SD.exists(filename));

            log_i("Opening `%s`", filename);

            logFile = SD.open(filename, FILE_APPEND);
            if (!logFile)
            {
                dest.drawString("ERROR", textX, textY - font->yAdvance, font);
                break;
            }
            display.fillCircle(MAP_REC_OFFSET, MAP_REC_OFFSET + statusBarFont->yAdvance, MAP_REC_SIZE, TFT_RED);
            display.setTextColor(TFT_BLACK);
            textdatum_t datum = display.getTextDatum();
            display.setTextDatum(middle_right);
            display.drawString(logFile.name(), display.width() - MAP_REC_OFFSET, statusBarFont->yAdvance + MAP_REC_OFFSET, &DejaVu24);
            display.setTextDatum(datum);

            // write a data header for conversion tools
            logFile.print("timestamp,latitude,longitude,altitude\n");
            isRecording = true;
            dest.drawString("Logging", textX, textY - font->yAdvance, font);
            currentMap.fillCircle(MAP_REC_OFFSET, MAP_REC_OFFSET, MAP_REC_SIZE, TFT_RED);
            currentMap.setTextColor(TFT_BLACK);
            currentMap.setTextDatum(middle_right);
            currentMap.drawString(logFile.name(), display.width() - MAP_REC_OFFSET, MAP_REC_OFFSET, &DejaVu24);
        }
        else if (!sdIsMounted)
            dest.drawString("No SD", textX, textY - font->yAdvance, font);
        break;
    case 1:
        dest.drawString("Home", textX, textY - font->yAdvance, font);
        dest.drawString("Saved", textX, textY, font);
        homeLatitude = gps.location.lat();
        homeLongitude = gps.location.lng();
        {
            String dummy;
            showStatusBar(currentBarType, dummy);
        }
        break;
    case 2:
        if (isRecording)
        {
            logFile.flush();
            log_i("Closing `%s` size: %lu bytes", filename, logFile.size());
            logFile.close();
            isRecording = false;
            dest.drawString("Stopped", textX, textY - font->yAdvance, font);
        }
        break;
    default:
        log_e("out of range button %i pressed?", buttonIndex);
        break;
    }
    vTaskDelay(pdMS_TO_TICKS(800)); // TODO: fix this so that the status bar is updated
    return true;
}

void loop()
{
    if (gps.location.isValid() && handleTouchScreen(display))
        drawMap(currentMap);

    static constexpr unsigned long GPS_TIMEOUT_MS = 6000;
    static unsigned long lastGpsUpdate = millis();
    if (!waitForNewGPSLocation(10))
    {
        if (millis() > 180000 && millis() - lastGpsUpdate > GPS_TIMEOUT_MS)
        {
            String result = "GPS Wiring or Antenna Error";
            showStatusBar(SHOW_STRING, result);
            lastGpsUpdate = millis();
        }
        return;
    }
    lastGpsUpdate = millis();

    static unsigned long lastSaveMS = 0;
    if (isRecording && millis() - lastSaveMS > SAVE_INTERVAL_MS)
    {
        logGPSData();
        lastSaveMS = millis();
    }

    String str;
    showStatusBar(currentBarType, str);

    static unsigned long lastUpdateMs = 0;
    if (millis() - lastUpdateMs > 100)
    {
        drawFreshMap(gps.location.lng(), gps.location.lat(), zoom);
        lastUpdateMs = millis();
    }
}
