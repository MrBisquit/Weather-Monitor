#include <Arduino.h>
#include <HTTPClient.h>
#include <weather_data.hpp>
#include <config.hpp>
#include <json.hpp>

using namespace io;
using namespace json;

bool weather_data::fetch(float* temp,
                    float* humidity,
                    float* pressure,
                    float* light,
                    float* wind_speed,
                    float* rain,
                    float* rain_per_second,
                    float* wind_direction,
                    time_t* last_updated) {
    HTTPClient client;
    client.begin(full_url);
    client.addHeader("wm_auth", wc_auth);
    if(0 >= client.GET()) {
        return false;
    }
    WiFiClient& wclient = client.getStream();
    // wrap the arduino Stream
    arduino_stream stm(&wclient);
    // use just 512 bytes of capture
    json_reader_ex<512> reader(stm);
    while(reader.read()) {
        if(reader.depth() == 1 && reader.node_type() == json::json_node_type::field) {
            if(temp!=nullptr && 0 == strcmp("temp", reader.value())) {
                reader.read();
                *temp = reader.value_real();
            } else if(humidity != nullptr && 0 == strcmp("humidity", reader.value())) {
                reader.read();
                *humidity = reader.value_real();
            } else if(pressure != nullptr && 0 == strcmp("pressure", reader.value())) {
                reader.read();
                *pressure = reader.value_int();
            } else if(light != nullptr && 0 == strcmp("light", reader.value())) {
                reader.read();
                *light = reader.value_int();
            } else if(wind_speed != nullptr && 0 == strcmp("wind_speed", reader.value())) {
                reader.read();
                *wind_speed = reader.value_int();
            } else if(rain != nullptr && 0 == strcmp("rain", reader.value())) {
                reader.read();
                *rain = reader.value_int();
            } else if(rain_per_second != nullptr && 0 == strcmp("rain_per_second", reader.value())) {
                reader.read();
                *rain_per_second = reader.value_int();
            } else if(wind_direction != nullptr && 0 == strcmp("wind_direction", reader.value())) {
                reader.read();
                *wind_direction = reader.value_int();
            } else if(last_updated != nullptr && 0 == strcmp("last_updated", reader.value())) {
                reader.read();
                *last_updated = (time_t)reader.value_int();
            }
        } else if(reader.depth() == 0) {
            // don't wait for end of document to terminate the connection
            break;
        }
    }
    client.end();
    return true;
    return false;
}