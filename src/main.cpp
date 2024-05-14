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

#define ICONS_IMPLEMENTATION // include/icons.hpp
#include <assets/icons.hpp>

#include <settings_config.hpp> // Settings
#include <SPIFFS.h> // FS (For mounting it)

// include this after everything else except ui.hpp
#include <config.hpp>
#include <ui.hpp>

#include <colours.hpp> // Colours

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
static bool time_fetching = false;

// for the color stuff
Colour_Theme colour_theme(THEME_LIGHT);

// the screen/control declarations
screen_t main_screen({320,240},sizeof(lcd_transfer_buffer1),lcd_transfer_buffer1,lcd_transfer_buffer2);
svg_clock_t ana_clock(main_screen);
label_t dig_clock(main_screen);
canvas_t topbar(main_screen);
//label_t topbar_battery(main_screen);
canvas_t wifi_icon(main_screen);
canvas_t battery_icon(main_screen);
canvas_t bottombar(main_screen);
push_button_t menu_button(main_screen);
push_button_t settings_button(main_screen);
push_button_t power_opts_button(main_screen);

// Low power screen
screen_t low_power_screen({320, 240},sizeof(lcd_transfer_buffer1), lcd_transfer_buffer1,lcd_transfer_buffer2);
label_t low_power_label(low_power_screen);

// Menu screen
screen_t menu_screen({320,240},sizeof(lcd_transfer_buffer1),lcd_transfer_buffer1,lcd_transfer_buffer2);

// Settings screen
screen_t settings_screen({320,240},sizeof(lcd_transfer_buffer1),lcd_transfer_buffer1,lcd_transfer_buffer2);
label_t settings_title(settings_screen);
push_button_t settings_home(settings_screen);
push_button_t settings_theme(settings_screen);
push_button_t settings_clock_type(settings_screen);

// Power options screen
screen_t power_opts_screen({320,240},sizeof(lcd_transfer_buffer1),lcd_transfer_buffer1,lcd_transfer_buffer2);
label_t power_opts_title(power_opts_screen);
push_button_t power_opts_home(power_opts_screen);
push_button_t power_opts_shutdown(power_opts_screen);
push_button_t power_opts_sleep(power_opts_screen);

// Weather data screen
int weather_data_screen_page = 0;
screen_t weather_data_screen({320,240},sizeof(lcd_transfer_buffer1),lcd_transfer_buffer1,lcd_transfer_buffer2);

push_button_t main_screen_fppb(main_screen);

label_t weather_data_title(weather_data_screen);
push_button_t weather_data_home(weather_data_screen);

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

static void wifi_icon_paint(surface_t& destination, const srect16& clip, void* state) {
    // if we're using the radio, indicate it with the appropriate icon
    /*if(time_fetching) {
        draw::icon(destination,point16::zero(),faWifi,color_t::light_gray);
    }*/

    if(WiFi.status() == WL_CONNECTED) {
        draw::icon(destination,point16::zero(),faWifi,color_t::green);
    } else if(WiFi.status() == WL_CONNECT_FAILED || WiFi.status() == WL_DISCONNECTED || WiFi.status() == WL_CONNECTION_LOST) {
        draw::icon(destination,point16::zero(),faWifi,color_t::red);
    } else {
        draw::icon(destination,point16::zero(),faWifi,color_t::white);
    }
}

/*static void battery_icon_paint(surface_t& destination, const srect16& clip, void* state) {
    // display the appropriate icon for the battery level
    // show in green if it's on ac power.
    int pct = power.battery_level();
    auto px = power.ac_in()?color_t::green:color_t::white;
    const const_bitmap<alpha_pixel<8>>* ico;
    if(pct<25) {
        ico = &faBatteryEmpty;
        if(!power.ac_in()) {
            px=color_t::red;
        }
    } else if(pct<50) {
        ico = &faBatteryQuarter;
    } else if(pct<75) {
        ico = &faBatteryHalf;
    } else if(pct<100) {
        ico = &faBatteryThreeQuarters;
    } else {
        ico = &faBatteryFull;
    }
    draw::icon(destination,point16::zero(),*ico,px);
}*/

static void battery_icon_paint(surface_t& destination, const srect16& clip, void* state) {
    // show in green if it's on ac power.
    int pct = power.battery_level();
    auto px = power.ac_in()?color_t::green:color_t::white;
   if(!power.ac_in() && pct<25) {
        px=color_t::red;
    }
    draw::icon(destination,point16::zero(),faBatteryEmpty,px);
    draw::filled_rectangle(destination,rect16(4,9,6+(0.14f*pct),14),px);
}

//char batt_str[8];
//int old_batt = 0;

// Top bar
static void topbar_paint(surface_t& destination, const gfx::srect16& clip, void* state) {
    draw::filled_rectangle(destination, destination.bounds(), colour_theme.primary, &clip); // color_t::dark_gray, colour_theme.secondary

    //int battery = power.battery_level();
    //old_batt = battery;
    
    //sprintf(batt_str, "%02d%", battery);
}

// Bottom bar
static void bottombar_paint(surface_t& destination, const gfx::srect16& clip, void* state) {
    draw::filled_rectangle(destination, destination.bounds(), colour_theme.primary, &clip); // color_t::dim_gray
}

// Testing
/*void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("\tSIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void testFileIO(fs::FS &fs, const char *path) {
  Serial.printf("Testing file I/O with %s\r\n", path);

  static uint8_t buf[512];
  size_t len = 0;
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }

  size_t i;
  Serial.print("- writing");
  uint32_t start = millis();
  for (i = 0; i < 2048; i++) {
    if ((i & 0x001F) == 0x001F) {
      Serial.print(".");
    }
    file.write(buf, 512);
  }
  Serial.println("");
  uint32_t end = millis() - start;
  Serial.printf(" - %u bytes written in %lu ms\r\n", 2048 * 512, end);
  file.close();

  file = fs.open(path);
  start = millis();
  end = start;
  i = 0;
  if (file && !file.isDirectory()) {
    len = file.size();
    size_t flen = len;
    start = millis();
    Serial.print("- reading");
    while (len) {
      size_t toRead = len;
      if (toRead > 512) {
        toRead = 512;
      }
      file.read(buf, toRead);
      if ((i++ & 0x001F) == 0x001F) {
        Serial.print(".");
      }
      len -= toRead;
    }
    Serial.println("");
    end = millis() - start;
    Serial.printf("- %u bytes read in %lu ms\r\n", flen, end);
    file.close();
  } else {
    Serial.println("- failed to open file for reading");
  }
}*/
// End Testing

static void reload_settings();
static void settings_reload();

/** -1 = Homepage (Clock)
 *   0 = Data     (Tapping on the clock screen)
 *   1 = Menu
 *   2 = Settings
 *   3 = Power options
 *   Others later
 * */
int selected_screen = -1;

// Menu button pressed
static void menu_button_pressed(bool pressed, void* state) {
    if(!pressed) return;

    selected_screen = 1;
}

// Settings button pressed
static void settings_button_pressed(bool pressed, void* state) {
    if(!pressed) return;

    selected_screen = 2;
    settings_reload();
    settings_screen.invalidate();
}

// Power options button pressed
static void power_opts_button_pressed(bool pressed, void* state) {
    if(!pressed) return;

    selected_screen = 3;
    power_opts_screen.invalidate();
}

// Power options buttons
// Power options home button
static void power_opts_home_pressed(bool pressed, void* state) {
    if(!pressed) return;

    selected_screen = -1;
    main_screen.invalidate();
}
// Power options shutdown button
static void power_opts_shutdown_pressed(bool pressed, void* state) {
    if(!pressed) return;

    power.power_off();
}
// Power options sleep button (Deep sleep)
static void power_opts_sleep_pressed(bool pressed, void* state) {
    if(!pressed) return;

    power.prepare_sleep();
    power.deep_sleep();
}

// Weather data screen (Wholescreen push_button)
static void weather_data_pb_pressed(bool pressed, void* state) {
    if(!pressed) return;

    selected_screen = 0;
    weather_data_screen_page = 0;
    weather_data_screen.invalidate();
}

static void weather_data_home_pressed(bool pressed, void* state) {
    if(!pressed) return;

    selected_screen = -1;
    main_screen.invalidate();
}

static void settings_home_pressed(bool pressed, void* state) {
    if(!pressed) return;

    selected_screen = -1;
    main_screen.invalidate();
}

static void settings_theme_pressed(bool pressed, void* state) {
    if(!pressed) return;

    if(SettingsFetchTheme() == THEME_DARK) {
        Serial.println("Changing theme to light...");
        SettingsSetTheme(THEME_LIGHT);
        Serial.println("Changed theme to light!");
    } else {
        Serial.println("Changing theme to dark...");
        SettingsSetTheme(THEME_DARK);
        Serial.println("Changed theme to dark!");
    }

    reload_settings();
    settings_reload();
    settings_screen.invalidate();
}

static void settings_clock_type_pressed(bool pressed, void* state) {
    if(!pressed) return;

    if(SettingsFetchClockType() == CLOCK_DIGITAL) {
        SettingsSetClockType(CLOCK_ANALOGUE);
    } else {
        SettingsSetClockType(CLOCK_DIGITAL);
    }

    reload_settings();
    settings_reload();
    settings_screen.invalidate();
}

static void reload_settings() {
    colour_theme.Change_Colour_Theme(SettingsFetchTheme());

    if(SettingsFetchClockType() == CLOCK_DIGITAL) {
        ana_clock.visible(false);
        dig_clock.visible(true);
    } else {
        ana_clock.visible(true);
        dig_clock.visible(false);
    }
}

static void settings_reload() {
    char theme_str[45];
    /*char theme[5];
    if(SettingsFetchTheme() == THEME_DARK) {
        //theme = "Dark";
        strcpy(theme, "Dark");
    } else {
        //theme = "Light";
        strcpy(theme, "Light");
    }*/
    
    //(SettingsFetchTheme() == THEME_DARK) ? "dark" : "light";
    
    //sprintf(theme_str, "Theme (Light/Dark, Selected: %s)", theme);
    //sprintf(theme_str, "Theme (Light/Dark) [%s]", theme);
    sprintf(theme_str, "Theme (Light/Dark) [%s]", SettingsFetchTheme() == THEME_DARK ? "Dark" : "Light");

    settings_theme.text(theme_str);

    char clock_type_str[46];
    /*char clock_type[7];
    if(SettingsFetchClockType() == CLOCK_DIGITAL) {
        //clock_type = "Digital";
        strcpy(clock_type, "Digital");
    } else {
        //clock_type = "Analog";
        strcpy(clock_type, "Analog");
    }*/

    //sprintf(clock_type_str, "Clock type (Digital/Analog, Selected: %s)", clock_type);
    //sprintf(clock_type_str, "Clock type (Dig/Ana) [%s]", clock_type);
    sprintf(clock_type_str, "Clock type (Dig/Ana) [%s]", SettingsFetchClockType() == CLOCK_DIGITAL ? "Digital" : "Analog");

    settings_clock_type.text(clock_type_str);
}

void setup()
{
    Serial.begin(115200);
    power.initialize(); // do this first
    lcd.initialize(); // do this next
    touch.initialize();
    touch.rotation(0);
    time_rtc.initialize();

    // Mount the FS
    // New place, up here so we can quickly fetch all of the config and set the colours correctly
    //SPIFFS.begin();
    Serial.println("Attempting SPIFFS mount...");
    if(!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");
        while(1);
    } else {
        Serial.println("SPIFFS mount succeeded");
    }

    // Fetch the colour theme
    colour_theme.Change_Colour_Theme(SettingsFetchTheme());
    
    // init the screen and callbacks
    main_screen.background_color(color_t::black);
    main_screen.on_flush_callback(lcd_flush);
    main_screen.wait_flush_callback(lcd_wait_flush);
    main_screen.on_touch_callback(lcd_touch);

    // init the analog clock, 128x128
    ana_clock.bounds(srect16(0,0,127,127).center_horizontal(main_screen.bounds()).center_vertical(main_screen.bounds())); // Added .center_vertical(main_screen.bounds())
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
    dig_clock.bounds(srect16(0,0,254,79).center_horizontal(main_screen.bounds()).center_vertical(main_screen.bounds())); // Removed .offset(0,128) and added .center_vertical(main_screen.bounds()), was srect16(0,0,127,39) now srect16(0,0,254,79)
    dig_clock.text_open_font(&text_font);
    dig_clock.text_line_height(80); // Was 40 now 80
    dig_clock.text_color(color32_t::white);
    dig_clock.text_justify(uix_justify::top_middle);
    main_screen.register_control(dig_clock);

    // Init the topbar
    topbar.bounds({0,0,319,23}); // Was 39 now 23
    topbar.on_paint_callback(topbar_paint);
    main_screen.register_control(topbar);
    /*topbar_battery.bounds({239,0,319,39});
    topbar_battery.padding({0,0});
    topbar_battery.text_open_font(&text_font);
    topbar_battery.text_line_height(35);
    //topbar_battery.text_justify(uix_justify::top_right);
    topbar_battery.text_color(color32_t::black);
    sprintf(batt_str,"%d",(int)power.battery_level());
    strcat(batt_str,"%");
    topbar_battery.text(batt_str);
    main_screen.register_control(topbar_battery);*/
    // set up a custom canvas for displaying our wifi icon
    wifi_icon.bounds(
        srect16(spoint16(0,0),(ssize16)wifi_icon.dimensions())
            .offset(main_screen.dimensions().width-
                wifi_icon.dimensions().width,0));
    wifi_icon.on_paint_callback(wifi_icon_paint);
    main_screen.register_control(wifi_icon);
    
    // set up a custom canvas for displaying our battery icon
    battery_icon.bounds(
        (srect16)faBatteryEmpty.dimensions().bounds());
    battery_icon.on_paint_callback(battery_icon_paint);
    main_screen.register_control(battery_icon);

    // Init the bottombar
    bottombar.bounds(srect16(0,0,319,39).offset(0, main_screen.dimensions().height - (bottombar.dimensions().height * 1.5)));
    bottombar.on_paint_callback(bottombar_paint);
    main_screen.register_control(bottombar);

    // Registering the bottombar nav buttons
    menu_button.background_color(colour_theme.secondary);
    settings_button.background_color(colour_theme.secondary);
    power_opts_button.background_color(colour_theme.secondary);

    gfx::rgba_pixel<32> transparent(0,0,0,0);

    menu_button.border_color(transparent);
    settings_button.border_color(transparent);
    power_opts_button.border_color(transparent);

    menu_button.text_color(colour_theme.background_text);
    settings_button.text_color(colour_theme.background_text);
    power_opts_button.text_color(colour_theme.background_text);

    menu_button.text("Menu");
    settings_button.text("Settings");
    power_opts_button.text("Power");

    menu_button.text_open_font(&text_font);
    settings_button.text_open_font(&text_font);
    power_opts_button.text_open_font(&text_font);

    /**
     * Starting width = 319 / 3 = 106.3
        Padding per rectangle (Left) = 3, (Right) = 3, (Total) = 6
        106.3 - 6 = 100.3

        All Y pos = .offset(0, main_screen.dimensions().height - (bottombar.dimensions().height * 1.5))
        First rect X = 3
        First rect X2 = 103.3

        Second rect X = (Old) 106.3 (New) 109.3
        Second rect X2 = (Old) 206.6 (New) 209.6

        Third rect X = (Old) 212.3 (New) 215.3
        Third rect X2 = (Old) 312.6 (New) 315.6

        All of their heights are 39 (40)

        (I think I'm right)
    */

    //menu_button.bounds(srect16(3, 0, 103.3, 39).offset(0, main_screen.dimensions().height - (bottombar.dimensions().height * 1.5)));
    //settings_button.bounds(srect16(106.3, 0, 206.3, 39).offset(0, main_screen.dimensions().height - (bottombar.dimensions().height * 1.5)));
    //power_opts_button.bounds(srect16(212.3, 0, 312.6, 39).offset(0, main_screen.dimensions().height - (bottombar.dimensions().height * 1.5)));#

    menu_button.bounds(srect16(3, 0, 103.3, 39).offset(0, main_screen.dimensions().height - (menu_button.dimensions().height * 1.5)));
    settings_button.bounds(srect16(109.3, 0, 209.3, 39).offset(0, main_screen.dimensions().height - (settings_button.dimensions().height * 1.5)));
    power_opts_button.bounds(srect16(215.3, 0, 315.6, 39).offset(0, main_screen.dimensions().height - (power_opts_button.dimensions().height * 1.5)));

    menu_button.on_pressed_changed_callback(menu_button_pressed);
    settings_button.on_pressed_changed_callback(settings_button_pressed);
    power_opts_button.on_pressed_changed_callback(power_opts_button_pressed);

    main_screen.register_control(menu_button);
    main_screen.register_control(settings_button);
    main_screen.register_control(power_opts_button);

    /*Serial.println("Attempting SPIFFS mount...");
    // Old place to mount FS
    if(!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");
        while(1);
    } else {
        Serial.println("SPIFFS mount succeeded");
    }*/

    // Now check the configs
    
    colour_theme.Change_Colour_Theme(SettingsFetchTheme());
    // Update all of the colours
    gfx::rgb_pixel<16> dst_bg;
    gfx::convert(colour_theme.background,&dst_bg);
    main_screen.background_color(dst_bg);
    dig_clock.text_color(colour_theme.background_text);
    topbar.invalidate();
    bottombar.invalidate();

    main_screen.invalidate();

    if(SettingsFetchClockType() == CLOCK_ANALOGUE) {
        dig_clock.visible(false);
    } else {
        ana_clock.visible(false);
    }

    // Initialise the low-power screen
    low_power_screen.background_color(color_t::black);
    low_power_screen.on_flush_callback(lcd_flush);
    low_power_screen.wait_flush_callback(lcd_wait_flush);
    low_power_screen.on_touch_callback(lcd_touch);

    low_power_label.bounds(srect16(0,0,254,79).center_horizontal(main_screen.bounds()).center_vertical(main_screen.bounds()));
    low_power_label.text_open_font(&text_font);
    low_power_label.text_line_height(60);
    low_power_label.text_color(color32_t::red);
    low_power_label.text_justify(uix_justify::top_middle);
    low_power_label.text("Low power");
    low_power_screen.register_control(low_power_label);

    // Initialise the menu screen
    menu_screen.background_color(dst_bg);
    menu_screen.on_flush_callback(lcd_flush);
    menu_screen.wait_flush_callback(lcd_wait_flush);
    menu_screen.on_touch_callback(lcd_touch);

    // Initialise the settings screen
    settings_screen.background_color(dst_bg);
    settings_screen.on_flush_callback(lcd_flush);
    settings_screen.wait_flush_callback(lcd_wait_flush);
    settings_screen.on_touch_callback(lcd_touch);

    // Initialise the power options screen
    power_opts_screen.background_color(dst_bg);
    power_opts_screen.on_flush_callback(lcd_flush);
    power_opts_screen.wait_flush_callback(lcd_wait_flush);
    power_opts_screen.on_touch_callback(lcd_touch);

    power_opts_title.bounds({10, 10, 191, 39});  // 127
    power_opts_title.text_open_font(&text_font);
    power_opts_title.text_line_height(30);
    power_opts_title.text_color(colour_theme.background_text);
    power_opts_title.text_justify(uix_justify::top_left);
    power_opts_title.text("Power options");
    power_opts_screen.register_control(power_opts_title);

    //                               Was 63.5
    power_opts_home.bounds(srect16(0, 0, 73.5, 39).offset(power_opts_screen.dimensions().width - (power_opts_home.dimensions().width * 1.5 + 10), 10));
    power_opts_home.text_open_font(&text_font);
    power_opts_home.text_color(colour_theme.background_text);
    power_opts_home.background_color(colour_theme.secondary);
    power_opts_home.border_color(transparent);
    power_opts_home.text("Home");
    power_opts_home.on_pressed_changed_callback(power_opts_home_pressed);
    power_opts_screen.register_control(power_opts_home);

    power_opts_shutdown.bounds(srect16(12, 60, 12 + 140, 60 + 170));
    power_opts_shutdown.text_open_font(&text_font);
    power_opts_shutdown.text_color(colour_theme.background_text);
    power_opts_shutdown.background_color(colour_theme.secondary);
    power_opts_shutdown.border_color(transparent);
    power_opts_shutdown.text("Shutdown");
    power_opts_shutdown.on_pressed_changed_callback(power_opts_shutdown_pressed);
    power_opts_screen.register_control(power_opts_shutdown);

    power_opts_sleep.bounds(srect16(167, 60, 167 + 140, 60 + 170));
    power_opts_sleep.text_open_font(&text_font);
    power_opts_sleep.text_color(colour_theme.background_text);
    power_opts_sleep.background_color(colour_theme.secondary);
    power_opts_sleep.border_color(transparent);
    power_opts_sleep.text("(Deep) Sleep");
    power_opts_sleep.on_pressed_changed_callback(power_opts_sleep_pressed);
    power_opts_screen.register_control(power_opts_sleep);

    // Registering the wholescreen thing (Excluding the topbar and bottombar)
    main_screen_fppb.bounds(srect16(0, 40, 320, 200));
    main_screen_fppb.background_color(transparent);
    main_screen_fppb.border_color(transparent);
    main_screen_fppb.pressed_background_color(transparent);
    main_screen_fppb.pressed_border_color(transparent);
    main_screen_fppb.on_pressed_changed_callback(weather_data_pb_pressed);
    main_screen.register_control(main_screen_fppb);

    // Registering the weather data stuff
    weather_data_screen.background_color(dst_bg);
    weather_data_screen.on_flush_callback(lcd_flush);
    weather_data_screen.wait_flush_callback(lcd_wait_flush);
    weather_data_screen.on_touch_callback(lcd_touch);

    weather_data_title.bounds({10, 10, 191, 39});  // 127
    weather_data_title.text_open_font(&text_font);
    weather_data_title.text_line_height(30);
    weather_data_title.text_color(colour_theme.background_text);
    weather_data_title.text_justify(uix_justify::top_left);
    weather_data_title.text("Weather data");
    weather_data_screen.register_control(weather_data_title);

    //                               Was 63.5
    weather_data_home.bounds(srect16(0, 0, 73.5, 39).offset(weather_data_screen.dimensions().width - (weather_data_home.dimensions().width * 1.5 + 10), 10));
    weather_data_home.text_open_font(&text_font);
    weather_data_home.text_color(colour_theme.background_text);
    weather_data_home.background_color(colour_theme.secondary);
    weather_data_home.border_color(transparent);
    weather_data_home.text("Home");
    weather_data_home.on_pressed_changed_callback(weather_data_home_pressed);
    weather_data_screen.register_control(weather_data_home);

    // Actually do the settings now
    settings_title.bounds({10, 10, 191, 39});  // 127
    settings_title.text_open_font(&text_font);
    settings_title.text_line_height(30);
    settings_title.text_color(colour_theme.background_text);
    settings_title.text_justify(uix_justify::top_left);
    settings_title.text("Settings");
    settings_screen.register_control(settings_title);

    settings_home.bounds(srect16(0, 0, 73.5, 39).offset(settings_screen.dimensions().width - (settings_home.dimensions().width * 1.5 + 10), 10));
    settings_home.text_open_font(&text_font);
    settings_home.text_color(colour_theme.background_text);
    settings_home.background_color(colour_theme.secondary);
    settings_home.border_color(transparent);
    settings_home.text("Home");
    settings_home.on_pressed_changed_callback(settings_home_pressed);
    settings_screen.register_control(settings_home);

    settings_theme.bounds(srect16(10, 50, 300 + 10, 40 + 50));
    settings_theme.text_open_font(&text_font);
    settings_theme.text_color(colour_theme.background_text);
    settings_theme.background_color(colour_theme.secondary);
    settings_theme.border_color(transparent);
    settings_theme.text("Theme (Light/Dark, Selected: ?)");
    settings_theme.on_pressed_changed_callback(settings_theme_pressed);
    settings_screen.register_control(settings_theme);

    settings_clock_type.bounds(srect16(10, 100, 300 + 10, 40 + 100));
    settings_clock_type.text_open_font(&text_font);
    settings_clock_type.text_color(colour_theme.background_text);
    settings_clock_type.background_color(colour_theme.secondary);
    settings_clock_type.border_color(transparent);
    settings_clock_type.text("Clock type (Analogue/Digital, Selected: ?)");
    settings_clock_type.on_pressed_changed_callback(settings_clock_type_pressed);
    settings_screen.register_control(settings_clock_type);

    // TESTING
    /*if(!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");
        while(1);
    }*/
    /*File file;
    if(SPIFFS.exists("/settings")) {
        file = SPIFFS.open("/settings","r");
        Serial.println(file.readString());
    } else {
        file = SPIFFS.open("/settings","w",true);
        file.println("hello");
        file.write(0);
        Serial.println("Wrote file. Restarting");
        ESP.restart();
    }
    file.close();

    listDir(SPIFFS, "/", 0);
    testFileIO(SPIFFS, "/test.txt");*/
}

void loop()
{
    if(power.battery_level() <= 5 && !power.ac_in()) {
        // Only update the low power screen
        low_power_screen.update();

        static uint32_t shutdown_ts = 0;

        if(shutdown_ts == 0 || millis() > (shutdown_ts + (60 * 1000))) {
            shutdown_ts = millis();

            power.power_off();
        }

        return;
    }

    ///////////////////////////////////
    // manage connection and fetching
    ///////////////////////////////////
    static int connection_state=0;
    static uint32_t refresh_ts = 0;
    static uint32_t time_ts = 0;
    switch(connection_state) { 
        case 0: // idle
        if(refresh_ts == 0 || millis() > (refresh_ts + (time_refresh_interval * 1000))) {
            refresh_ts = millis();
            connection_state = 1;
            time_ts = 0;
        }
        break;
        case 1: // connecting
            if(WiFi.status() != WL_CONNECTED) {
                Serial.println("Connecting to network...");
                if(wifi_ssid == nullptr) {
                    WiFi.begin();
                } else {
                    WiFi.begin(wifi_ssid,wifi_pass);
                }
                connection_state = 2;
            } else if(WiFi.status()==WL_CONNECTED) {
                connection_state = 2;
            }
            break;
        case 2: // connected
            if(WiFi.status() == WL_CONNECTED) {
                Serial.println("Connected.");
                connection_state = 3;
            } else if(WiFi.status() == WL_CONNECT_FAILED) {
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

                // Make the icon invalid
                wifi_icon.invalidate(); //So it updates
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
    if(selected_screen == -1) {
        main_screen.update();
    } else if(selected_screen == 0) {
        weather_data_screen.update();
    } else if(selected_screen == 1) {
        menu_screen.update();
    } else if(selected_screen == 2) {
        settings_screen.update();
    } else if(selected_screen == 3) {
        power_opts_screen.update();
    }

    // Figure out if we need to redraw the topbar
    /*if(old_batt != (int)power.battery_level()) {
        sprintf(batt_str,"%d%%",(int)power.battery_level());
        // Requires redrawing
        topbar_battery.invalidate();
    }*/

    // update the battery level
    static int bat_level = power.battery_level();
    if((int)power.battery_level()!=bat_level) {
        bat_level = power.battery_level();
        battery_icon.invalidate();
    }
    static bool ac_in = power.ac_in();
    if((int)power.battery_level()!=ac_in) {
        ac_in = power.ac_in();
        battery_icon.invalidate();
    }

    // Checking the WiFi connection every second
    static uint32_t wifi_refresh_ts = 0;
    static uint32_t wifi_time_ts = 0;
    static wl_status_t wifi_status = WiFi.status();
    if(wifi_refresh_ts == 0 || millis() > (wifi_refresh_ts + 1000)) {
        wifi_refresh_ts = millis();
        wifi_time_ts = 0;

        if(WiFi.status() != wifi_status) {
            wifi_icon.invalidate();
        }
    }
}