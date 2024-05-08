#include <Arduino.h>
#include <m5core2_power.hpp>
#include <bm8563.hpp>
#include <ft6336.hpp>
#include <tft_io.hpp>
#include <ili9341.hpp>
#include <uix.hpp>
#include <gfx.hpp>
#include <WiFi.h>
#include <ip_loc.hpp>
#include <ntp_time.hpp>
#define OPENSANS_REGULAR_IMPLEMENTATION
#include <assets/OpenSans_Regular.hpp>
// include this after everything else except ui.hpp
#include <config.hpp>
#include <ui.hpp>
using namespace arduino;
using namespace gfx;
using namespace uix;
// for AXP192 power management (required for core2)
static m5core2_power power;

// for the LCD
using tft_bus_t = arduino::tft_spi_ex<VSPI,5,23,-1,18,0,false,320*240*2+8>;
using lcd_t = arduino::ili9342c<15,-1,-1,tft_bus_t,1>;
static lcd_t lcd;
// use two 32KB buffers (DMA)
static uint8_t lcd_transfer_buffer1[32*1024];
static uint8_t lcd_transfer_buffer2[32*1024];

// for the touch panel
using touch_t = arduino::ft6336<280,320>;
static touch_t touch(Wire1);

// for the time stuff
static bm8563 time_rtc(Wire1);
static char time_buffer[32];
static long time_offset = 0;
static IPAddress time_server_ip;
static ntp_time time_server;

// the screen/control declarations
screen_t main_screen({320,240},sizeof(lcd_transfer_buffer1),lcd_transfer_buffer1,lcd_transfer_buffer2);
svg_clock_t ana_clock(main_screen);
label_t dig_clock(main_screen);

// for dumping to the display (UIX)
static void lcd_flush(const rect16& bounds,const void* bmp,void* state) {
    const const_bitmap<decltype(lcd)::pixel_type> cbmp(bounds.dimensions(),bmp);
    draw::bitmap_async(lcd,bounds,cbmp,cbmp.bounds());
}
// for display DMA (UIX/GFX)
static void lcd_wait_flush(void* state) {
    lcd.wait_all_async();
}
// for the touch panel
static void lcd_touch(point16* out_locations,size_t* in_out_locations_size,void* state) {
    if(touch.update()) {
        *in_out_locations_size = 0;
        uint16_t x,y;
        if(touch.xy(&x,&y)) {
            Serial.printf("(%d,%d)\n",x,y);
            out_locations[0]=point16(x,y);
            ++*in_out_locations_size;
            if(touch.xy2(&x,&y)) {
                out_locations[1]=point16(x,y);
                ++*in_out_locations_size;
            }
        }
    }
}
// updates the time string with the current time
static void update_time_buffer(time_t time) {
    tm tim = *localtime(&time);
    strftime(time_buffer, sizeof(time_buffer), "%I:%M %p", &tim);
    if(*time_buffer=='0') {
        *time_buffer=' ';
    }
}
// grabs the timezone offset based on IP
static void fetch_time_offset() {
    ip_loc::fetch(nullptr,nullptr,&time_offset,nullptr,0,nullptr,0);
}

char batt_str[8];
int old_batt = 0;

// Top bar
static void topbar_paint(surface_t& destination, const gfx::srect16& clip, void* state) {
    draw::filled_rectangle(destination, destination.bounds() ,color_t::dark_gray, &clip);

    int battery = power.battery_level();
    old_batt = battery;
    
    sprintf(batt_str, "%02d%", battery);
}

void setup()
{
    Serial.begin(115200);
    power.initialize(); // do this first
    lcd.initialize(); // do this next
    touch.initialize();
    touch.rotation(0);
    time_rtc.initialize();
    
    // init the screen and callbacks
    main_screen.background_color(color_t::black);
    main_screen.on_flush_callback(lcd_flush);
    main_screen.wait_flush_callback(lcd_wait_flush);
    main_screen.on_touch_callback(lcd_touch);

    // init the analog clock, 128x128
    ana_clock.bounds(srect16(0,0,127,127).center_horizontal(main_screen.bounds()));
    // make the second hand semi-transparent
    auto px = ana_clock.second_color();
    // use pixel metadata to figure out what half of the max value is
    px.template channel<channel_name::A>(decltype(px)::channel_by_name<channel_name::A>::max/2);
    ana_clock.second_color(px);

    // do similar with the minute hand as the second hand
    px = ana_clock.minute_color();
    // same as above, but it handles it for you, using a scaled float
    px.template channelr<channel_name::A>(0.5f);
    ana_clock.minute_color(px);
    main_screen.register_control(ana_clock);

    // init the digital clock, 128x40, below the analog clock
    dig_clock.bounds(srect16(0,0,127,39).center_horizontal(main_screen.bounds()).offset(0,128));
    dig_clock.text_open_font(&text_font);
    dig_clock.text_line_height(40);
    dig_clock.text_color(color32_t::white);
    dig_clock.text_justify(uix_justify::top_middle);
    main_screen.register_control(dig_clock);

    // Init the topbar
    topbar.on_paint_callback(topbar_paint);
    topbar.bounds({0,0,319,39});
    main_screen.register_control(topbar);
    topbar_battery.text_open_font(&text_font);
    topbar_battery.text_line_height(40);
    topbar_battery.text(batt_str);
    main_screen.register_control(topbar_battery);
}

void loop()
{
    ///////////////////////////////////
    // manage connection and fetching
    ///////////////////////////////////
    static int connection_state=0;
    static uint32_t refresh_ts = 0;
    static uint32_t time_ts = 0;
    switch(connection_state) { 
        case 0: // idle
        if(refresh_ts==0 || millis() > (refresh_ts+(time_refresh_interval*1000))) {
            refresh_ts = millis();
            connection_state = 1;
            time_ts = 0;
        }
        break;
        case 1: // connecting
            if(WiFi.status()!=WL_CONNECTED) {
                Serial.println("Connecting to network...");
                if(wifi_ssid==nullptr) {
                    WiFi.begin();
                } else {
                    WiFi.begin(wifi_ssid,wifi_pass);
                }
                connection_state =2;
            } else if(WiFi.status()==WL_CONNECTED) {
                connection_state = 2;
            }
            break;
        case 2: // connected
            if(WiFi.status()==WL_CONNECTED) {
                Serial.println("Connected.");
                connection_state = 3;
            } else if(WiFi.status()==WL_CONNECT_FAILED) {
                refresh_ts = 0; // immediately try to connect again
                connection_state = 0;
            }
            break;
        case 3: // fetch
            Serial.println("Retrieving time info...");
            refresh_ts = millis();
            fetch_time_offset();
            WiFi.hostByName(time_server_domain,time_server_ip);
            connection_state = 4;
            time_ts = millis(); // we're going to correct for latency
            time_server.begin_request(time_server_ip);
            break;
        case 4: // polling for response
            if(time_server.request_received()) {
                const int latency_offset = (millis()-time_ts)/1000;
                time_rtc.set((time_t)(time_server.request_result()+time_offset+latency_offset));
                Serial.println("Clock set.");
                // prime the digital clock
                update_time_buffer(time_rtc.now());
                dig_clock.text(time_buffer);
                dig_clock.invalidate();
                connection_state = 0;
                Serial.println("Turning WiFi off.");
                WiFi.disconnect(true,false);
            }
            break;
    }
    ///////////////////
    // update the UI
    //////////////////
    time_t time = time_rtc.now();
    ana_clock.time(time);
    // only update every minute (efficient)
    if(0==(time%60)) {
        update_time_buffer(time);
        // tell the label the text changed
        dig_clock.invalidate();
    }
    //////////////////////////
    // pump various objects
    /////////////////////////
    time_server.update();
    main_screen.update();

    // Figure out if we need to redraw the topbar
    if(old_batt != (int)power.battery_level()) {
        // Requires redrawing
        topbar.invalidate();
    }
}