#include <settings_config.hpp>
#include <SPIFFS.h>

struct ConfigSettings {
    Clock clock_type;
    Theme theme;
};

ConfigSettings cfg;
bool cfg_loaded = false;

// Forward declarations
void LoadSettingsConfig();
void WriteSettingsConfig();

Clock SettingsFetchClockType() {
    if(!cfg_loaded) LoadSettingsConfig();

    return cfg.clock_type;
}

Theme SettingsFetchTheme() {
    if(!cfg_loaded) LoadSettingsConfig();

    return cfg.theme;
}

void SettingsSetClockType(Clock clock_type) {
    cfg.clock_type = clock_type;

    WriteSettingsConfig();
}

void SettingsSetTheme(Theme theme) {
    cfg.theme = theme;

    WriteSettingsConfig();
}

void LoadSettingsConfig() {
    if(SPIFFS.exists("/config")) {
        File file = SPIFFS.open("/config","rb");
        if(sizeof(cfg) != file.read((uint8_t*) & cfg, sizeof(cfg))) {
            // invalid config
            // should delete it and continue with defaults
            // for reasons
            cfg.clock_type = CLOCK_ANALOGUE;
            cfg.theme = THEME_DARK;
        } else {
            // cfg has your stored data
        }
    }

    cfg_loaded = true;
}

void WriteSettingsConfig() {
    if(SPIFFS.exists("/config")) {
        SPIFFS.remove("/config");
    }

    File file = SPIFFS.open("/config","wb");
    file.write((uint8_t*) & cfg, sizeof(cfg));
    file.close();
}