#ifndef SNAKE_GAME_H
#define SNAKE_GAME_H

#include "IGame.h"
#include "Arduboy2ESP.h"

class SnakeGame : public IGame {
public:
    static SnakeGame& getInstance();

    const char* getName() const override { return "Snake"; }
    void setup() override;
    void loop() override;
    void onExit() override;

private:
    SnakeGame() = default;

    Arduboy2 arduboy;
    enum class State { Title, Playing, GameOver };
    State m_state{State::Title};

    static constexpr int CELL_SIZE = 4;
    static constexpr int GRID_W = 128 / CELL_SIZE; // 32
    static constexpr int GRID_H = 64 / CELL_SIZE;  // 16

    struct Point {
        int x, y;
    };

    Point m_snake[100];
    int m_length{3};
    int m_dirX{1};
    int m_dirY{0};

    Point m_food{15, 8};
    int m_score{0};
    int m_moveTimer{0};
    int m_moveInterval{6}; // Move every 6 frames (10 fps speed)

    void resetGame();
    void spawnFood();
    void updatePlaying();
    void drawPlaying();
};

#endif // SNAKE_GAME_H
