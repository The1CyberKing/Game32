#include "EmulatorState.h"
#include "InputManager.h"
#include "StateManager.h"
#include "MenuState.h"
#include "DisplayManager.h"
#include "types.h"
#include <esp_log.h>

static const char* TAG = "EmulatorState";

EmulatorState& EmulatorState::getInstance() {
    static EmulatorState instance;
    return instance;
}

void EmulatorState::setTargetRom(const std::string& fullPath) {
    m_targetRom = fullPath;
}

void EmulatorState::onEnter() {
    ESP_LOGI(TAG, "Entering Emulator State. Target ROM: %s", m_targetRom.c_str());
    sysContext.current_state.store(SystemState::EmulatorRunning);
    
    DisplayManager::getInstance().clearBuffer();
    DisplayManager::getInstance().drawText(0, 0, "EMULATOR STUB");
    DisplayManager::getInstance().drawText(0, 10, m_targetRom.c_str());
    DisplayManager::getInstance().drawText(0, 20, "Press B to return");
    DisplayManager::getInstance().renderPipelinePush();
}

void EmulatorState::onUpdate() {
    // Press B to return to menu
    if (InputManager::getInstance().justPressed(5)) { // BTN_B
        StateManager::getInstance().changeState(&MenuState::getInstance());
        return;
    }
}

void EmulatorState::onDraw() {
    // Handled in onEnter for now
}

void EmulatorState::onExit() {
    ESP_LOGI(TAG, "Exiting Emulator State...");
}