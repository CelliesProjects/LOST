#ifndef NETWORK_DETAILS_H
#define NETWORK_DETAILS_H

#include <Arduino.h>

struct NetworkDetails
{
    const char *ssid;
    const char *password;

    constexpr NetworkDetails(const char *s, const char *p) : ssid(s), password(p) {}

    NetworkDetails() = delete;
};

#endif
