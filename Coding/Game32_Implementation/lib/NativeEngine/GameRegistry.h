#ifndef GAME_REGISTRY_H
#define GAME_REGISTRY_H

#include "IGame.h"
#include <vector>

class GameRegistry {
public:
    static GameRegistry& getInstance();
    
    void registerGame(IGame* game);
    const std::vector<IGame*>& getAllGames() const;
    IGame* getGameByName(const char* name) const;
    IGame* getGameByIndex(size_t index) const;
    size_t getGameCount() const;

private:
    GameRegistry();
    std::vector<IGame*> m_games;
};

#endif // GAME_REGISTRY_H
