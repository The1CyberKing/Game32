#include "BlackjackGame.h"
#include "src/Game.h"
#include "esp_log.h"

static const char* TAG = "BlackjackGame";

static Game g_blackjackGame;

BlackjackGame& BlackjackGame::getInstance() {
    static BlackjackGame instance;
    return instance;
}

void BlackjackGame::setup() {
    ESP_LOGI(TAG, "Initializing native Arduboy Blackjack Game...");
    g_blackjackGame.setup();
}

void BlackjackGame::loop() {
    g_blackjackGame.loop();
}

void BlackjackGame::onExit() {
    ESP_LOGI(TAG, "Exiting native Arduboy Blackjack Game...");
}
