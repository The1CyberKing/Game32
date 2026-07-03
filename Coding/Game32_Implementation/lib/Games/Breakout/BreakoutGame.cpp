#include "BreakoutGame.h"
#include "AudioEngine.h"
#include "SDManager.h"
#include <cstdio>
#include <cmath>

BreakoutGame& BreakoutGame::getInstance() {
    static BreakoutGame instance;
    return instance;
}

void BreakoutGame::setup() {
    arduboy.begin();
    arduboy.setFrameRate(60);
    m_state = State::Title;
    m_score = 0;
    m_lives = 3;
}

void BreakoutGame::initLevel() {
    m_bricksRemaining = NUM_ROWS * NUM_COLS;
    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            m_bricks[r][c] = true;
        }
    }
    resetBall();
}

void BreakoutGame::resetBall() {
    m_paddleX = (128 - PADDLE_W) / 2;
    m_ballX = m_paddleX + PADDLE_W / 2;
    m_ballY = PADDLE_Y - 4;
    m_ballVX = 1.5f;
    m_ballVY = -1.5f;
}

void BreakoutGame::updatePlaying() {
    if (arduboy.pressed(LEFT_BUTTON) || arduboy.pressed(A_BUTTON)) {
        m_paddleX -= 3;
        if (m_paddleX < 0) m_paddleX = 0;
    }
    if (arduboy.pressed(RIGHT_BUTTON) || arduboy.pressed(B_BUTTON)) {
        m_paddleX += 3;
        if (m_paddleX + PADDLE_W > 128) m_paddleX = 128 - PADDLE_W;
    }

    m_ballX += m_ballVX;
    m_ballY += m_ballVY;

    // Wall bounce
    if (m_ballX <= 0) {
        m_ballX = 0;
        m_ballVX = -m_ballVX;
        AudioEngine::getInstance().playTone(400, 0); delay(10); AudioEngine::getInstance().stopTone();
    } else if (m_ballX + BALL_SIZE >= 128) {
        m_ballX = 128 - BALL_SIZE;
        m_ballVX = -m_ballVX;
        AudioEngine::getInstance().playTone(400, 0); delay(10); AudioEngine::getInstance().stopTone();
    }

    if (m_ballY <= 0) {
        m_ballY = 0;
        m_ballVY = -m_ballVY;
        AudioEngine::getInstance().playTone(400, 0); delay(10); AudioEngine::getInstance().stopTone();
    }

    // Paddle bounce
    if (m_ballY + BALL_SIZE >= PADDLE_Y && m_ballY <= PADDLE_Y + PADDLE_H) {
        if (m_ballX + BALL_SIZE >= m_paddleX && m_ballX <= m_paddleX + PADDLE_W) {
            m_ballY = PADDLE_Y - BALL_SIZE;
            m_ballVY = -std::abs(m_ballVY);
            // Angle based on hit offset
            float hitOffset = (m_ballX - (m_paddleX + PADDLE_W / 2.0f)) / (PADDLE_W / 2.0f);
            m_ballVX = hitOffset * 2.5f;
            AudioEngine::getInstance().playTone(600, 0); delay(15); AudioEngine::getInstance().stopTone();
        }
    }

    // Brick collisions
    int brickW = 12;
    int brickH = 5;
    int startX = 4;
    int startY = 10;

    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            if (m_bricks[r][c]) {
                int bx = startX + c * brickW;
                int by = startY + r * brickH;
                if (m_ballX + BALL_SIZE >= bx && m_ballX <= bx + brickW - 1 &&
                    m_ballY + BALL_SIZE >= by && m_ballY <= by + brickH - 1) {
                    m_bricks[r][c] = false;
                    m_bricksRemaining--;
                    m_score += 10;
                    m_ballVY = -m_ballVY;
                    AudioEngine::getInstance().playTone(800 + (r * 150), 0); delay(15); AudioEngine::getInstance().stopTone();
                    if (m_bricksRemaining == 0) {
                        m_state = State::Victory;
                        AudioEngine::getInstance().playTone(1000, 1200); delay(200); AudioEngine::getInstance().stopTone();
                    }
                    return;
                }
            }
        }
    }

    // Bottom fall through
    if (m_ballY > 64) {
        m_lives--;
        AudioEngine::getInstance().playTone(200, 150); delay(200); AudioEngine::getInstance().stopTone();
        if (m_lives <= 0) {
            m_state = State::GameOver;
        } else {
            resetBall();
            delay(500);
        }
    }
}

void BreakoutGame::drawPlaying() {
    // Draw bricks
    int brickW = 12;
    int brickH = 5;
    int startX = 4;
    int startY = 10;

    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            if (m_bricks[r][c]) {
                arduboy.fillRect(startX + c * brickW, startY + r * brickH, brickW - 1, brickH - 1, WHITE);
            }
        }
    }

    // Draw paddle & ball
    arduboy.fillRect(m_paddleX, PADDLE_Y, PADDLE_W, PADDLE_H, WHITE);
    arduboy.fillRect((int)m_ballX, (int)m_ballY, BALL_SIZE, BALL_SIZE, WHITE);

    // Draw HUD
    char hud[32];
    snprintf(hud, sizeof(hud), "SCORE:%d  LIVES:%d", m_score, m_lives);
    arduboy.setCursor(2, 0);
    arduboy.print(hud);
}

void BreakoutGame::loop() {
    if (!arduboy.nextFrame()) return;
    arduboy.pollButtons();
    arduboy.clear();

    switch (m_state) {
        case State::Title:
            arduboy.setCursor(32, 15);
            arduboy.print("BREAKOUT");
            arduboy.setCursor(15, 35);
            arduboy.print("Press A or B to Start");
            if (arduboy.justPressed(A_BUTTON) || arduboy.justPressed(B_BUTTON)) {
                m_score = 0;
                m_lives = 3;
                initLevel();
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
                m_score = 0;
                m_lives = 3;
                initLevel();
                m_state = State::Playing;
            }
            break;

        case State::Victory:
            arduboy.setCursor(35, 20);
            arduboy.print("YOU WIN!");
            snprintf(scoreStr, sizeof(scoreStr), "Score: %d", m_score);
            arduboy.setCursor(38, 35);
            arduboy.print(scoreStr);
            arduboy.setCursor(15, 50);
            arduboy.print("Press A to Play Again");
            if (arduboy.justPressed(A_BUTTON) || arduboy.justPressed(B_BUTTON)) {
                initLevel();
                m_state = State::Playing;
            }
            break;
    }
    arduboy.display();
}

void BreakoutGame::onExit() {
    AudioEngine::getInstance().stopTone();
}

struct BreakoutSaveData {
    int state;
    bool bricks[4][10];
    int bricksRemaining;
    int paddleX;
    float ballX;
    float ballY;
    float ballVX;
    float ballVY;
    int score;
    int lives;
};

bool BreakoutGame::saveState(int slot) {
    BreakoutSaveData data;
    data.state = static_cast<int>(m_state);
    for(int r=0; r<NUM_ROWS; r++) {
        for(int c=0; c<NUM_COLS; c++) {
            data.bricks[r][c] = m_bricks[r][c];
        }
    }
    data.bricksRemaining = m_bricksRemaining;
    data.paddleX = m_paddleX;
    data.ballX = m_ballX;
    data.ballY = m_ballY;
    data.ballVX = m_ballVX;
    data.ballVY = m_ballVY;
    data.score = m_score;
    data.lives = m_lives;
    return SDManager::getInstance().saveGameState(getName(), slot, (const uint8_t*)&data, sizeof(data));
}

bool BreakoutGame::loadState(int slot) {
    BreakoutSaveData data;
    if (!SDManager::getInstance().loadGameState(getName(), slot, (uint8_t*)&data, sizeof(data))) {
        return false;
    }
    m_state = static_cast<State>(data.state);
    for(int r=0; r<NUM_ROWS; r++) {
        for(int c=0; c<NUM_COLS; c++) {
            m_bricks[r][c] = data.bricks[r][c];
        }
    }
    m_bricksRemaining = data.bricksRemaining;
    m_paddleX = data.paddleX;
    m_ballX = data.ballX;
    m_ballY = data.ballY;
    m_ballVX = data.ballVX;
    m_ballVY = data.ballVY;
    m_score = data.score;
    m_lives = data.lives;
    return true;
}
