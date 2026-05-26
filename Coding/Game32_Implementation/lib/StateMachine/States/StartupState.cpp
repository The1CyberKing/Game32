#include "StartupState.h"
#include "DisplayManager.h"
#include "SDManager.h"
#include "StateManager.h"
#include "MenuState.h"
#include "types.h"
#include <stdio.h>
#include <esp_log.h>

static const char* TAG = "StartupState";

StartupState& StartupState::getInstance() {
    static StartupState instance; // Statically allocated in memory forever
    return instance;
}

void StartupState::onEnter() {
    ESP_LOGI(TAG, "Entering Startup Sequence...");
    m_frameCount = 0;
    sysContext.current_state.store(SystemState::Startup);
    
    if (!SDManager::getInstance().initialize()) {
        ESP_LOGE(TAG, "Failed to initialize SD Card");
    }
}

void StartupState::onUpdate() {
    m_frameCount++;
    if (m_frameCount > 90) {
        StateManager::getInstance().changeState(&MenuState::getInstance());
    }
}

void StartupState::onDraw() {
    uint16_t raw_tenths = sysContext.battery_percentage_tenths.load();
    uint16_t whole_percentage = raw_tenths / 10;
    uint16_t decimal_fraction = raw_tenths % 10;

    char battery_str[24];
    snprintf(battery_str, sizeof(battery_str), "BATT %d.%d PERCENT", whole_percentage, decimal_fraction);

    DisplayManager::getInstance().drawString(10, 10, "GAME32 OS");
    DisplayManager::getInstance().drawString(10, 25, "SYSTEM BOOT...");
    DisplayManager::getInstance().drawString(10, 45, battery_str);
}

void StartupState::onExit() {
    ESP_LOGI(TAG, "Exiting Startup Sequence.");
}