#include "OverlayState.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "InputManager.h"
#include "../../NativeEngine/EEPROM.h"
#include "../../NativeEngine/Arduboy2ESP.h"
#include "DisplayManager.h"
#include "AudioEngine.h"
#include "SDManager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdio>
#include <string>

static const char* TAG = "OverlayState";

OverlayState& OverlayState::getInstance() {
    static OverlayState instance;
    return instance;
}

void OverlayState::onEnter() {
    ESP_LOGI(TAG, "Entering System Overlay");
    m_active = true;
    m_beepEndTimeMs = 0;
    EEPROM.commitToFile(Arduboy2ESP::getGameName());
    m_cursorIndex = 0; // Start at Resume
    
    // Short enter beep
    AudioEngine::getInstance().playTone(1000, 0);
    m_beepEndTimeMs = (uint32_t)(esp_timer_get_time() / 1000) + 80;
}

void OverlayState::onUpdate() {
    uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);
    if (m_beepEndTimeMs > 0 && nowMs >= m_beepEndTimeMs) {
        AudioEngine::getInstance().stopTone();
        m_beepEndTimeMs = 0;
    }

    auto& im = InputManager::getInstance();

    // UP button (0)
    if (im.justPressed(0)) {
        m_cursorIndex = (m_cursorIndex - 1 + NUM_ITEMS) % NUM_ITEMS;
        AudioEngine::getInstance().playTone(600, 0);
        m_beepEndTimeMs = nowMs + 40;
        return;
    }

    // DOWN button (1)
    if (im.justPressed(1)) {
        m_cursorIndex = (m_cursorIndex + 1) % NUM_ITEMS;
        AudioEngine::getInstance().playTone(600, 0);
        m_beepEndTimeMs = nowMs + 40;
        return;
    }

    // LEFT button (2)
    if (im.justPressed(2) || im.isHeld(2)) {
        if (m_cursorIndex == 3) { // Volume
            AudioEngine::getInstance().volumeDown();
            uint8_t vol = AudioEngine::getInstance().getVolume();
            AudioEngine::getInstance().playTone(400 + (vol * 30), 0);
            m_beepEndTimeMs = nowMs + 40;
        } else if (m_cursorIndex == 4) { // Brightness
            // DisplayManager::getInstance().brightnessDown();
            // uint8_t brg = DisplayManager::getInstance().getBrightness();
            // AudioEngine::getInstance().playTone(400 + (brg * 12), 0);
            m_beepEndTimeMs = nowMs + 40;
        }
        return;
    }

    // RIGHT button (3)
    if (im.justPressed(3) || im.isHeld(3)) {
        if (m_cursorIndex == 3) { // Volume
            AudioEngine::getInstance().volumeUp();
            uint8_t vol = AudioEngine::getInstance().getVolume();
            AudioEngine::getInstance().playTone(400 + (vol * 30), 0);
            m_beepEndTimeMs = nowMs + 40;
        } else if (m_cursorIndex == 4) { // Brightness
            // DisplayManager::getInstance().brightnessUp();
            // uint8_t brg = DisplayManager::getInstance().getBrightness();
            // AudioEngine::getInstance().playTone(400 + (brg * 12), 0);
            m_beepEndTimeMs = nowMs + 40;
        }
        return;
    }

    // START button (6) anywhere resumes immediately
    if (im.justPressed(6)) {
        ESP_LOGI(TAG, "START pressed. Resuming game.");
        AudioEngine::getInstance().stopTone();
        resume();
        return;
    }

    // A button (4) or B button (5) or SELECT button (7) activates selected item
    if (im.justPressed(4) || im.justPressed(5) || im.justPressed(7)) {
        switch (m_cursorIndex) {
            case 0: // Resume
                ESP_LOGI(TAG, "Resume selected. Returning to game.");
                AudioEngine::getInstance().stopTone();
                resume();
                break;
            case 1: // Save State
                ESP_LOGI(TAG, "Save State selected.");
                AudioEngine::getInstance().playTone(1200, 0);
                m_beepEndTimeMs = nowMs + 150;
                /*
                if (GameEngineState::getInstance().getTargetGame()) {
                    IGame* g = GameEngineState::getInstance().getTargetGame();
                    int slot = SDManager::getInstance().getPrioritySlot(g->getName());
                    g->saveState(slot);
                }
                */
                break;
            case 2: // Load State
                ESP_LOGI(TAG, "Load State selected.");
                AudioEngine::getInstance().playTone(1200, 0);
                m_beepEndTimeMs = nowMs + 150;
                /*
                if (GameEngineState::getInstance().getTargetGame()) {
                    IGame* g = GameEngineState::getInstance().getTargetGame();
                    int slot = SDManager::getInstance().getPrioritySlot(g->getName());
                    if (g->loadState(slot)) {
                        StateManager::getInstance().changeState(&GameEngineState::getInstance());
                    }
                }
                */
                break;
            case 3: // Volume (LEFT/RIGHT ONLY)
            case 4: // Brightness (LEFT/RIGHT ONLY)
                break;
            case 5: // Quit to Menu
                ESP_LOGI(TAG, "Quit to Menu selected. Rebooting to ota_0...");
                AudioEngine::getInstance().stopTone();
                EEPROM.commitToFile(Arduboy2ESP::getGameName());
                {
                    const esp_partition_t* launcher_part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
                    if (launcher_part) {
                        esp_ota_set_boot_partition(launcher_part);
                        esp_restart();
                    } else {
                        ESP_LOGE(TAG, "FATAL: ota_0 partition not found!");
                    }
                }
                break;
        }
        return;
    }
}

void OverlayState::onDraw() {
    // Note: Do NOT call clearBuffer() so the underlying paused game remains visible!
    
    // Draw filled black window box in the center (x=2, y=1, w=124, h=62)
    DisplayManager::getInstance().fillRect(2, 1, 124, 62, 0);
    
    // Draw white border around box
    DisplayManager::getInstance().drawLine(2, 1, 125, 1, 1);   // Top
    DisplayManager::getInstance().drawLine(2, 62, 125, 62, 1); // Bottom
    DisplayManager::getInstance().drawLine(2, 1, 2, 62, 1);    // Left
    DisplayManager::getInstance().drawLine(125, 1, 125, 62, 1); // Right

    // Draw Title
    DisplayManager::getInstance().drawString(28, 3, "- GAME MENU -");
    DisplayManager::getInstance().drawLine(2, 11, 125, 11, 1);

    // Items
    const int y_positions[6] = { 13, 20, 27, 37, 44, 54 };
    
    // Dividers
    DisplayManager::getInstance().drawLine(2, 35, 125, 35, 1);
    DisplayManager::getInstance().drawLine(2, 52, 125, 52, 1);
    
    bool is_blink_on = (esp_timer_get_time() / 400000) % 2 == 0;
    
    for (int i = 0; i < NUM_ITEMS; i++) {
        int y = y_positions[i];
        bool selected = (i == m_cursorIndex);
        uint8_t text_color = 1;
        uint8_t bg_color = 0;
        uint8_t draw_color = 1;
        
        if (selected && is_blink_on) {
            DisplayManager::getInstance().fillRect(3, y - 1, 122, 8, 1);
            text_color = 0;
            bg_color = 1;
            draw_color = 0;
        } else if (selected) {
            DisplayManager::getInstance().drawString(4, y, ">");
        }
        
        char itemStr[32];
        if (i == 0) {
            snprintf(itemStr, sizeof(itemStr), "Resume");
        } else if (i == 1) {
            snprintf(itemStr, sizeof(itemStr), "Save State");
        } else if (i == 2) {
            snprintf(itemStr, sizeof(itemStr), "Load State");
        } else if (i == 3) {
            uint8_t vol = AudioEngine::getInstance().getVolume();
            snprintf(itemStr, sizeof(itemStr), "Vol:%3d%%", vol * 5);
        } else if (i == 4) {
            // uint8_t brg = DisplayManager::getInstance().getBrightness();
            snprintf(itemStr, sizeof(itemStr), "Brg: N/A");
        } else if (i == 5) {
            snprintf(itemStr, sizeof(itemStr), "Quit to Menu");
        }
        
        int text_x = (selected && is_blink_on) ? 10 : 12;
        DisplayManager::getInstance().drawString(text_x, y, itemStr, text_color, bg_color);
        
        // Render horizontal progress bar for Volume & Brightness
        if (i == 3 || i == 4) {
            int bar_x0 = 62;
            int bar_x1 = 118;
            DisplayManager::getInstance().drawLine(bar_x0, y + 1, bar_x1, y + 1, draw_color);
            DisplayManager::getInstance().drawLine(bar_x0, y + 5, bar_x1, y + 5, draw_color);
            DisplayManager::getInstance().drawLine(bar_x0, y + 1, bar_x0, y + 5, draw_color);
            DisplayManager::getInstance().drawLine(bar_x1, y + 1, bar_x1, y + 5, draw_color);
            
            int fill_w = 0;
            if (i == 3) {
                uint8_t vol = AudioEngine::getInstance().getVolume();
                fill_w = ((int)vol * 54) / 20;
            } else {
                // uint8_t brg = DisplayManager::getInstance().getBrightness();
                // fill_w = ((int)brg * 54) / 50;
                fill_w = 0;
            }
            if (fill_w > 0) {
                DisplayManager::getInstance().fillRect(bar_x0 + 1, y + 2, fill_w, 3, draw_color);
            }
        }
    }
}

void OverlayState::onExit() {
    ESP_LOGI(TAG, "Exiting System Overlay");
    AudioEngine::getInstance().stopTone();
}
