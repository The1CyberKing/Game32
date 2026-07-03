#include "GameRegistry.h"
#include <cstring>

#include "GamesRegistration.h"

GameRegistry& GameRegistry::getInstance() {
    static GameRegistry instance;
    return instance;
}

GameRegistry::GameRegistry() {
    registerBuiltInGames(*this);
}

void GameRegistry::registerGame(IGame* game) {
    if (game) {
        m_games.push_back(game);
    }
}

const std::vector<IGame*>& GameRegistry::getAllGames() const {
    return m_games;
}

IGame* GameRegistry::getGameByName(const char* name) const {
    for (IGame* g : m_games) {
        if (strcmp(g->getName(), name) == 0) {
            return g;
        }
    }
    return nullptr;
}

IGame* GameRegistry::getGameByIndex(size_t index) const {
    if (index < m_games.size()) {
        return m_games[index];
    }
    return nullptr;
}

size_t GameRegistry::getGameCount() const {
    return m_games.size();
}
