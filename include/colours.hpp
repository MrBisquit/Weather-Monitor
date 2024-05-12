#include <ui.hpp>
#include "settings_config.hpp"

#pragma once

struct Colour_Theme {
    /*Colour_Theme(Theme theme = THEME_DARK) {
        if(theme == THEME_LIGHT) {
            basic_primary = color_t::light_gray;
            basic_secondary = color_t::gray;
        } else {

        }
    }*/
    Colour_Theme(Theme theme = THEME_DARK);
    void Change_Colour_Theme(Theme theme = THEME_DARK);

    gfx::rgba_pixel<32>  primary;
    gfx::rgba_pixel<32>  secondary;
    gfx::rgba_pixel<32>  background;
    gfx::rgba_pixel<32>  background_text;
};