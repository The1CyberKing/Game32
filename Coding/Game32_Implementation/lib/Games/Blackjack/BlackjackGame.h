#ifndef BLACKJACK_GAME_H
#define BLACKJACK_GAME_H

#include "IGame.h"

class Game; // Forward declaration of native Arduboy Game class

class BlackjackGame : public IGame {
public:
    static BlackjackGame& getInstance();
    
    const char* getName() const override { return "Blackjack"; }
    void setup() override;
    void loop() override;
    void onExit() override;

private:
    BlackjackGame() = default;
    Game* m_game{nullptr};
};

#endif // BLACKJACK_GAME_H
