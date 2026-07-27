#include "EEPROM.h"
#include <stdio.h>
#include <string>
#include "esp_log.h"

EEPROMClass EEPROM;

static const char* TAG = "EEPROM";

uint8_t EEPROMClass::read(int address) {
    if (address < 0 || address >= 1024) return 0;
    return m_data[address];
}

void EEPROMClass::write(int address, uint8_t val) {
    if (address < 0 || address >= 1024) return;
    m_data[address] = val;
}

void EEPROMClass::update(int address, uint8_t val) {
    if (address < 0 || address >= 1024) return;
    if (m_data[address] != val) {
        m_data[address] = val;
    }
}

void EEPROMClass::loadFromFile(const char* gameName) {
    if (!gameName || gameName[0] == '\0') {
        ESP_LOGE(TAG, "Cannot load EEPROM: gameName is null or empty.");
        return;
    }
    std::string path = std::string("/sd/saves/") + gameName + ".sav";
    FILE* f = fopen(path.c_str(), "rb");
    if (f) {
        fread(m_data, 1, 1024, f);
        fclose(f);
        ESP_LOGI(TAG, "Loaded EEPROM from %s", path.c_str());
    } else {
        ESP_LOGW(TAG, "No save file found at %s. Using blank EEPROM.", path.c_str());
    }
}

void EEPROMClass::commitToFile(const char* gameName) {
    if (!gameName || gameName[0] == '\0') {
        ESP_LOGE(TAG, "Cannot commit EEPROM: gameName is null or empty.");
        return;
    }
    std::string path = std::string("/sd/saves/") + gameName + ".sav";
    FILE* f = fopen(path.c_str(), "wb");
    if (f) {
        fwrite(m_data, 1, 1024, f);
        fclose(f);
        ESP_LOGI(TAG, "Committed EEPROM to %s", path.c_str());
    } else {
        ESP_LOGE(TAG, "Failed to write save file to %s", path.c_str());
    }
}
