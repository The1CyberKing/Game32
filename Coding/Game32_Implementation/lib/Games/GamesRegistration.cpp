#include "GamesRegistration.h"
#include "GameRegistry.h"
#include "Blackjack/BlackjackGame.h"
#include "Breakout/BreakoutGame.h"
#include "Snake/SnakeGame.h"
#include "SpaceInvaders/SpaceInvadersGame.h"

void registerBuiltInGames(GameRegistry& registry) {
    registry.registerGame(&BlackjackGame::getInstance());
    registry.registerGame(&BreakoutGame::getInstance());
    registry.registerGame(&SnakeGame::getInstance());
    registry.registerGame(&SpaceInvadersGame::getInstance());
}
