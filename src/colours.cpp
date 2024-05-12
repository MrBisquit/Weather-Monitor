#include <ui.hpp>
#include <colours.hpp>
#include "settings_config.hpp"

using namespace gfx;

Colour_Theme::Colour_Theme(Theme theme) {
    Change_Colour_Theme(theme);
}

void Colour_Theme::Change_Colour_Theme(Theme theme) {
    if(theme == THEME_LIGHT) {
        /*primary = color32_t::light_gray;
        secondary = color32_t::gray;
        background = color32_t::white;
        background_text = color32_t::black;*/

        primary         = rgba_pixel<32>(238, 238, 238, 255);
        secondary       = rgba_pixel<32>(218, 218, 218, 255);
        background      = color32_t::white;
        background_text = color32_t::black;
    } else {
        /*primary = color32_t::dim_gray;
        secondary = color32_t::dark_slate_gray;
        background = color32_t::black;
        background_text = color32_t::white;*/

        primary         = rgba_pixel<32>(23, 23, 23, 255);
        secondary       = rgba_pixel<32>(55, 55, 55, 255);
        background      = color32_t::black;
        background_text = color32_t::white;
    }
}