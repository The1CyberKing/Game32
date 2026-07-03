#include "GameEngineState.h"
#include "MenuState.h"
#include "OverlayState.h"
#include "../StateManager.h"
#include "InputManager.h"
#include "AudioEngine.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "GameEngineState";

GameEngineState& GameEngineState::getInstance() {
    static GameEngineState instance;
    return instance;
}

void GameEngineState::setTargetGame(IGame* game) {
    if (m_activeGame != game) {
        m_activeGame = game;
        m_gameInitialized = false;
    }
}

void GameEngineState::pauseToOverlay() {
    ESP_LOGI(TAG, "Pausing game to System Overlay...");
    m_isPausedToOverlay = true;
    StateManager::getInstance().changeState(&OverlayState::getInstance());
}

void GameEngineState::quitGame() {
    ESP_LOGI(TAG, "Quitting active game.");
    if (m_activeGame && m_gameInitialized) {
        m_activeGame->onExit();
    }
    m_gameInitialized = false;
    m_isPausedToOverlay = false;
}

void GameEngineState::onEnter() {
    ESP_LOGI(TAG, "Entering GameEngineState");
    if (!m_activeGame) {
        ESP_LOGE(TAG, "No target game set! Returning to Menu.");
        StateManager::getInstance().changeState(&MenuState::getInstance());
        return;
    }
    if (!m_gameInitialized) {
        ESP_LOGI(TAG, "Starting native game: %s", m_activeGame->getName());
        m_activeGame->setup();
        m_gameInitialized = true;
    } else {
        ESP_LOGI(TAG, "Resuming native game without wiping state: %s", m_activeGame->getName());
    }
    m_isPausedToOverlay = false;
    m_lastInputTime = esp_timer_get_time();
}

void GameEngineState::onUpdate() {
    if (!m_activeGame) {
        StateManager::getInstance().changeState(&MenuState::getInstance());
        return;
    }

    // Non-blocking 150ms input cooldown upon enter/resume to prevent menu button presses from leaking into game
    if ((esp_timer_get_time() - m_lastInputTime) < 150000ULL) {
        return;
    }

    // Hold START + SELECT to pause and open System Overlay
    if (InputManager::getInstance().isHeld(6) && InputManager::getInstance().isHeld(7)) {
        pauseToOverlay();
        return;
    }

    m_activeGame->loop();
}

void GameEngineState::onDraw() {
    // Drawing is handled inside the native game loop via arduboy.display()
}

void GameEngineState::onExit() {
    ESP_LOGI(TAG, "Exiting GameEngineState");
    if (!m_isPausedToOverlay) {
        if (m_activeGame && m_gameInitialized) {
            m_activeGame->onExit();
            m_gameInitialized = false;
        }
    }
    AudioEngine::getInstance().stopTone();
}
