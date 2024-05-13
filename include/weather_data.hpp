#pragma once

#include <time.h>

struct weather_data final {
    static bool fetch(float* temp,
                    float* humidity,
                    float* pressure,
                    float* light,
                    float* wind_speed,
                    float* rain,
                    float* rain_per_second,
                    float* wind_direction,
                    time_t* last_updated);
};

/**
 * {
    "temp": 25.52,
    "humidity": 43.12,
    "pressure": 970.16,
    "light": 1501.56,
    "wind_speed": 1.626947,
    "rain": 0,
    "rain_per_second": 0,
    "wind_direction": 45,
    "last_updated": 1715527805000,
    "success": true
}
*/