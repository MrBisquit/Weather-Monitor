#pragma once
// ☣︎ No touching or it goes boom

// Clock types
enum Clock {
    CLOCK_ANALOGUE = 1,
    CLOCK_DIGITAL = 2
};

// Themes
enum Theme {
    THEME_LIGHT = 1,
    THEME_DARK = 2,
};

Clock SettingsFetchClockType();
Theme SettingsFetchTheme();

void SettingsSetClockType(Clock clock_type);
void SettingsSetTheme(Theme theme);