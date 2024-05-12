#pragma once
#include <gfx.hpp>
#include <uix.hpp>
// colors for the UI
using color_t = gfx::color<gfx::rgb_pixel<16>>; // native
using color32_t = gfx::color<gfx::rgba_pixel<32>>; // uix

// the screen definition
using screen_t = uix::screen<gfx::rgb_pixel<16>>;
using surface_t = screen_t::control_surface_type;

// the control definitions
using svg_clock_t = uix::svg_clock<surface_t>;
using label_t = uix::label<surface_t>;
using canvas_t = uix::canvas<surface_t>;
using push_button_t = uix::push_button<surface_t>;

extern screen_t main_screen;
extern svg_clock_t ana_clock;
extern label_t dig_clock;

extern canvas_t topbar;
extern canvas_t wifi_icon;
extern canvas_t battery_icon;
//extern label_t topbar_battery;

//extern char batt_str[];

extern canvas_t bottombar;
extern push_button_t menu_button;
extern push_button_t settings_button;
extern push_button_t power_opts_button;

// Low power screen
// Display when charge % is >=5
extern screen_t low_power_screen;
extern label_t low_power_label;

// Menu screen
extern screen_t menu_screen;

// Settings screen
extern screen_t settings_screen;

// Power options screen
extern screen_t power_opts_screen;
extern label_t power_opts_title;
extern push_button_t power_opts_home;