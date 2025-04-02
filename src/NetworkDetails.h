#ifndef NETWORK_DETAILS_H
#define NETWORK_DETAILS_H

#include <Arduino.h>

struct NetworkDetails
{
    String ssid;
    String password;

    NetworkDetails() = default;
    NetworkDetails(const char* s, const char* p) : ssid(s), password(p) {}
};

#endif