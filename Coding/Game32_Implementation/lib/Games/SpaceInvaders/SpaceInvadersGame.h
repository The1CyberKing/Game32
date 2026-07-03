#ifndef SPACE_INVADERS_GAME_H
#define SPACE_INVADERS_GAME_H

#include "IGame.h"
#include "Arduboy2ESP.h"

class SpaceInvadersGame : public IGame {
public:
    static SpaceInvadersGame& getInstance();

    const char* getName() const override { return "Space Invaders"; }
    void setup() override;
    void loop() override;
    void onExit() override;

private:
    SpaceInvadersGame() = default;

    Arduboy2 arduboy;
    enum class State { Title, Playing, GameOver, Victory };
    State m_state{State::Title};

    int m_playerX{56};
    static constexpr int PLAYER_W = 11;
    static constexpr int PLAYER_H = 5;
    static constexpr int PLAYER_Y = 56;

    struct Bullet {
        float x, y;
        bool active;
    };
    Bullet m_bullet{0, 0, false};

    static constexpr int NUM_COLS = 6;
    static constexpr int NUM_ROWS = 3;
    bool m_invaders[NUM_ROWS][NUM_COLS];
    int m_invaderX{10};
    int m_invaderY{10};
    int m_invaderDir{1};
    int m_moveTimer{0};
    int m_moveInterval{20};
    int m_invadersRemaining{0};

    int m_score{0};
    int m_lives{3};

    void initLevel();
    void updatePlaying();
    void drawPlaying();
};

#endif // SPACE_INVADERS_GAME_H
