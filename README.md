# LOST GPS logger

LOST is a lightweight GPS logging and mapping tool built for ESP32 devices. It combines real-time OpenStreetMap rendering with GPS tracking and SD card logging in a compact, touch-friendly interface.

![LOSTboot](https://github.com/user-attachments/assets/74453081-2dcb-4764-8027-8fac877fa1d9)  ![LOSTlogging](https://github.com/user-attachments/assets/aa3da55d-3aa6-42d1-9d12-b49e8398cfa2) ![LOSThome](https://github.com/user-attachments/assets/bc1533f4-0297-4343-a5f1-34552c6355f4)



The project showcases:

- Tile-based OSM map rendering using LovyanGFX OpenStreetMap and PNGdec
- GPS tracking via TinyGPS++
- Status bar with local time, GPS fix info
- Progressive confirmation buttons using LovyanGFX

Currently only tested on a M5stack Core2 with a M5Stack GNNS module.

## Installing this app

This is a [PlatformIO](https://platformio.org/) project, you will need VSCode with the PlatformIO plugin installed.

### Libraries needed

This project depends on [LovyanGFX](https://github.com/lovyan03/LovyanGFX), [OpenStreetMap](https://github.com/CelliesProjects/OpenStreetMap-esp32), [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) and [PNGdec](https://github.com/bitbank2/PNGdec) to get the work done. 

### Set your network details

Before flashing the following source should be adjusted to your needs and saved as `src/secrets.h`.

```c++
#ifndef SECRETS_H
#define SECRETS_H

#include "NetworkDetails.h"

double homeLongitude = 0.0;
double homeLatitude = 0.0;

// Central European Time - see https://remotemonitoringsystems.ca/time-zone-abbreviations.php
constexpr char *TIME_ZONE = "CET-1CEST,M3.5.0/2,M10.5.0/3";

// Change `nl` to your country iso code for better latency
constexpr char *NTP_POOL = "nl.pool.ntp.org";

constexpr NetworkDetails net1("wifi network1", "wifi network 1 password");
constexpr NetworkDetails net2("wifi network2", "wifi network 2 password");

// All networks defined above must be added here
constexpr NetworkDetails knownNetworks[] = {net1, net2};

#endif

```

## License differences between this library and the map data

### This library has a MIT license

The `LOST` project -this project- is licensed under the [MIT license](/LICENSE).

### The downloaded tile data has a Open Data Commons Open Database License (ODbL)

OpenStreetMap® is open data, licensed under the [Open Data Commons Open Database License (ODbL)](https://opendatacommons.org/licenses/odbl/) by the OpenStreetMap Foundation (OSMF).

Use of any OSMF provided service is governed by the [OSMF Terms of Use](https://osmfoundation.org/wiki/Terms_of_Use).

This project is not endorsed by or affiliated with the OpenStreetMap Foundation.
