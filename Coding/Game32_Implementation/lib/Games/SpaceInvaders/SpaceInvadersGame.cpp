#include "SpaceInvadersGame.h"
#include "AudioEngine.h"
#include <cstdio>
#include <cmath>

SpaceInvadersGame& SpaceInvadersGame::getInstance() {
    static SpaceInvadersGame instance;
    return instance;
}

void SpaceInvadersGame::setup() {
    arduboy.begin();
    arduboy.setFrameRate(60);
    m_state = State::Title;
}

void SpaceInvadersGame::initLevel() {
    m_invadersRemaining = NUM_ROWS * NUM_COLS;
    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            m_invaders[r][c] = true;
        }
    }
    m_invaderX = 10;
    m_invaderY = 10;
    m_invaderDir = 1;
    m_moveInterval = 20;
    m_moveTimer = 0;
    m_bullet.active = false;
    m_playerX = 56;
}

void SpaceInvadersGame::updatePlaying() {
    // Player movement
    if (arduboy.pressed(LEFT_BUTTON) || arduboy.pressed(A_BUTTON)) {
        m_playerX -= 2;
        if (m_playerX < 0) m_playerX = 0;
    }
    if (arduboy.pressed(RIGHT_BUTTON) || arduboy.pressed(B_BUTTON)) {
        m_playerX += 2;
        if (m_playerX + PLAYER_W > 128) m_playerX = 128 - PLAYER_W;
    }

    // Shoot bullet
    if ((arduboy.justPressed(B_BUTTON) || arduboy.justPressed(UP_BUTTON)) && !m_bullet.active) {
        m_bullet.x = m_playerX + PLAYER_W / 2;
        m_bullet.y = PLAYER_Y - 2;
        m_bullet.active = true;
        AudioEngine::getInstance().playTone(1200, 0); delay(10); AudioEngine::getInstance().stopTone();
    }

    // Move bullet
    if (m_bullet.active) {
        m_bullet.y -= 4;
        if (m_bullet.y < 0) {
            m_bullet.active = false;
        } else {
            // Check invader collisions
            int invW = 10;
            int invH = 6;
            for (int r = 0; r < NUM_ROWS; r++) {
                for (int c = 0; c < NUM_COLS; c++) {
                    if (m_invaders[r][c]) {
                        int ix = m_invaderX + c * 14;
                        int iy = m_invaderY + r * 10;
                        if (m_bullet.x >= ix && m_bullet.x <= ix + invW &&
                            m_bullet.y >= iy && m_bullet.y <= iy + invH) {
                            m_invaders[r][c] = false;
                            m_bullet.active = false;
                            m_invadersRemaining--;
                            m_score += 20;
                            AudioEngine::getInstance().playTone(400, 0); delay(15); AudioEngine::getInstance().stopTone();
                            
                            // Speed up as invaders die
                            m_moveInterval = 5 + (m_invadersRemaining * 15) / (NUM_ROWS * NUM_COLS);

                            if (m_invadersRemaining == 0) {
                                m_state = State::Victory;
                                AudioEngine::getInstance().playTone(800, 1000); delay(200); AudioEngine::getInstance().stopTone();
                            }
                            return;
                        }
                    }
                }
            }
        }
    }

    // Move invaders
    m_moveTimer++;
    if (m_moveTimer >= m_moveInterval) {
        m_moveTimer = 0;
        m_invaderX += m_invaderDir * 4;

        // Check bounds and drop down
        bool drop = false;
        for (int r = 0; r < NUM_ROWS; r++) {
            for (int c = 0; c < NUM_COLS; c++) {
                if (m_invaders[r][c]) {
                    int ix = m_invaderX + c * 14;
                    if (ix < 2 || ix + 10 > 126) {
                        drop = true;
                    }
                }
            }
        }

        if (drop) {
            m_invaderDir = -m_invaderDir;
            m_invaderX += m_invaderDir * 4;
            m_invaderY += 4;
            AudioEngine::getInstance().playTone(150, 0); delay(15); AudioEngine::getInstance().stopTone();
        } else {
            AudioEngine::getInstance().playTone(200, 0); delay(10); AudioEngine::getInstance().stopTone();
        }

        // Check if invaders reached bottom
        for (int r = 0; r < NUM_ROWS; r++) {
            for (int c = 0; c < NUM_COLS; c++) {
                if (m_invaders[r][c]) {
                    int iy = m_invaderY + r * 10;
                    if (iy + 6 >= PLAYER_Y) {
                        m_lives--;
                        AudioEngine::getInstance().playTone(100, 0); delay(300); AudioEngine::getInstance().stopTone();
                        if (m_lives <= 0) {
                            m_state = State::GameOver;
                        } else {
                            initLevel();
                            delay(500);
                        }
                        return;
                    }
                }
            }
        }
    }
}

void SpaceInvadersGame::drawPlaying() {
    // Draw player ship
    arduboy.fillRect(m_playerX, PLAYER_Y, PLAYER_W, PLAYER_H, WHITE);
    arduboy.fillRect(m_playerX + 4, PLAYER_Y - 2, 3, 2, WHITE);

    // Draw bullet
    if (m_bullet.active) {
        arduboy.fillRect((int)m_bullet.x, (int)m_bullet.y, 2, 4, WHITE);
    }

    // Draw invaders
    int invW = 10;
    int invH = 6;
    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            if (m_invaders[r][c]) {
                int ix = m_invaderX + c * 14;
                int iy = m_invaderY + r * 10;
                arduboy.fillRect(ix, iy, invW, invH, WHITE);
                // Simple alien eyes (black dots)
                arduboy.drawPixel(ix + 2, iy + 2, BLACK);
                arduboy.drawPixel(ix + 7, iy + 2, BLACK);
            }
        }
    }

    // Draw score
    char hud[32];
    snprintf(hud, sizeof(hud), "SCORE:%d  LIVES:%d", m_score, m_lives);
    arduboy.setCursor(2, 0);
    arduboy.print(hud);
}

void SpaceInvadersGame::loop() {
    if (!arduboy.nextFrame()) return;
    arduboy.pollButtons();
    arduboy.clear();

    switch (m_state) {
        case State::Title:
            arduboy.setCursor(22, 15);
            arduboy.print("SPACE INVADERS");
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

void SpaceInvadersGame::onExit() {
    AudioEngine::getInstance().stopTone();
}
