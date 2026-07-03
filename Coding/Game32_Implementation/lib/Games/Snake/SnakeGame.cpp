#include "SnakeGame.h"
#include "AudioEngine.h"
#include <cstdio>
#include <cstdlib>

SnakeGame& SnakeGame::getInstance() {
    static SnakeGame instance;
    return instance;
}

void SnakeGame::setup() {
    arduboy.begin();
    arduboy.setFrameRate(60);
    m_state = State::Title;
}

void SnakeGame::spawnFood() {
    bool valid = false;
    while (!valid) {
        m_food.x = rand() % GRID_W;
        m_food.y = rand() % GRID_H;
        valid = true;
        for (int i = 0; i < m_length; i++) {
            if (m_snake[i].x == m_food.x && m_snake[i].y == m_food.y) {
                valid = false;
                break;
            }
        }
    }
}

void SnakeGame::resetGame() {
    m_length = 4;
    for (int i = 0; i < m_length; i++) {
        m_snake[i] = {10 - i, 8};
    }
    m_dirX = 1;
    m_dirY = 0;
    m_score = 0;
    m_moveTimer = 0;
    spawnFood();
}

void SnakeGame::updatePlaying() {
    // Input handling (prevent 180 degree reversal)
    if (arduboy.pressed(UP_BUTTON) && m_dirY == 0) {
        m_dirX = 0; m_dirY = -1;
    } else if (arduboy.pressed(DOWN_BUTTON) && m_dirY == 0) {
        m_dirX = 0; m_dirY = 1;
    } else if (arduboy.pressed(LEFT_BUTTON) && m_dirX == 0) {
        m_dirX = -1; m_dirY = 0;
    } else if (arduboy.pressed(RIGHT_BUTTON) && m_dirX == 0) {
        m_dirX = 1; m_dirY = 0;
    }

    m_moveTimer++;
    if (m_moveTimer < m_moveInterval) return;
    m_moveTimer = 0;

    // Calculate new head position
    Point nextHead = { m_snake[0].x + m_dirX, m_snake[0].y + m_dirY };

    // Check wall collisions
    if (nextHead.x < 0 || nextHead.x >= GRID_W || nextHead.y < 0 || nextHead.y >= GRID_H) {
        m_state = State::GameOver;
        AudioEngine::getInstance().playTone(200, 150); delay(300); AudioEngine::getInstance().stopTone();
        return;
    }

    // Check self collision
    for (int i = 0; i < m_length; i++) {
        if (m_snake[i].x == nextHead.x && m_snake[i].y == nextHead.y) {
            m_state = State::GameOver;
            AudioEngine::getInstance().playTone(200, 150); delay(300); AudioEngine::getInstance().stopTone();
            return;
        }
    }

    // Check food collision
    bool ateFood = (nextHead.x == m_food.x && nextHead.y == m_food.y);
    if (ateFood) {
        if (m_length < 99) m_length++;
        m_score += 10;
        spawnFood();
        AudioEngine::getInstance().playTone(1000, 0); delay(20); AudioEngine::getInstance().stopTone();
    }

    // Move body
    for (int i = m_length - 1; i > 0; i--) {
        m_snake[i] = m_snake[i - 1];
    }
    m_snake[0] = nextHead;
}

void SnakeGame::drawPlaying() {
    // Draw food
    arduboy.fillRect(m_food.x * CELL_SIZE, m_food.y * CELL_SIZE, CELL_SIZE - 1, CELL_SIZE - 1, WHITE);

    // Draw snake
    for (int i = 0; i < m_length; i++) {
        arduboy.fillRect(m_snake[i].x * CELL_SIZE, m_snake[i].y * CELL_SIZE, CELL_SIZE - 1, CELL_SIZE - 1, WHITE);
    }

    // Draw score in corner
    char scoreStr[16];
    snprintf(scoreStr, sizeof(scoreStr), "%d", m_score);
    arduboy.setCursor(110, 0);
    arduboy.print(scoreStr);
}

void SnakeGame::loop() {
    if (!arduboy.nextFrame()) return;
    arduboy.pollButtons();
    arduboy.clear();

    switch (m_state) {
        case State::Title:
            arduboy.setCursor(45, 15);
            arduboy.print("SNAKE");
            arduboy.setCursor(15, 35);
            arduboy.print("Press A or B to Start");
            if (arduboy.justPressed(A_BUTTON) || arduboy.justPressed(B_BUTTON)) {
                resetGame();
                m_state = State::Playing;
            }
            break;

        case State::Playing:
            updatePlaying();
            drawPlaying();
            break;

        case State::GameOver:
            arduboy.setCursor(35, 20);
            arduboy.print("GAME OVER");
            char scoreStr[32];
            snprintf(scoreStr, sizeof(scoreStr), "Score: %d", m_score);
            arduboy.setCursor(38, 35);
            arduboy.print(scoreStr);
            arduboy.setCursor(15, 50);
            arduboy.print("Press A to Play Again");
            if (arduboy.justPressed(A_BUTTON) || arduboy.justPressed(B_BUTTON)) {
                resetGame();
                m_state = State::Playing;
            }
            break;
    }
    arduboy.display();
}

void SnakeGame::onExit() {
    AudioEngine::getInstance().stopTone();
}
