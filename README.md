# LOST GPS logger

[LGFX](https://github.com/lovyan03/LovyanGFX)-[OSM](https://github.com/CelliesProjects/OpenStreetMap-esp32)-[TinyGPS](https://github.com/mikalhart/TinyGPSPlus) and [PNGdec](https://github.com/bitbank2/PNGdec)

This is a demo app for the [OSM-esp32](https://github.com/CelliesProjects/OpenStreetMap-esp32) library.

Currently only tested on a M5stack Core2 with a M5Stack GNNS module.

## Installing this app

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
