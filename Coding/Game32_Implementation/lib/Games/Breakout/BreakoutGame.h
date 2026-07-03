#ifndef BREAKOUT_GAME_H
#define BREAKOUT_GAME_H

#include "IGame.h"
#include "Arduboy2ESP.h"

class BreakoutGame : public IGame {
public:
    static BreakoutGame& getInstance();

    const char* getName() const override { return "Breakout"; }
    void setup() override;
    void loop() override;
    void onExit() override;
    bool saveState(int slot) override;
    bool loadState(int slot) override;

private:
    BreakoutGame() = default;

    Arduboy2 arduboy;
    enum class State { Title, Playing, GameOver, Victory };
    State m_state{State::Title};

    static constexpr int NUM_COLS = 10;
    static constexpr int NUM_ROWS = 4;
    bool m_bricks[NUM_ROWS][NUM_COLS];
    int m_bricksRemaining{0};

    int m_paddleX{52};
    static constexpr int PADDLE_W = 24;
    static constexpr int PADDLE_H = 3;
    static constexpr int PADDLE_Y = 60;

    float m_ballX{64.0f};
    float m_ballY{32.0f};
    float m_ballVX{1.5f};
    float m_ballVY{-1.5f};
    static constexpr int BALL_SIZE = 2;

    int m_score{0};
    int m_lives{3};

    void initLevel();
    void resetBall();
    void updatePlaying();
    void drawPlaying();
};

#endif // BREAKOUT_GAME_H
