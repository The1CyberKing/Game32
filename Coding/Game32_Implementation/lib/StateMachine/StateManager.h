#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include "IGameState.h"

class StateManager {
public:
    static StateManager& getInstance();
    StateManager(const StateManager&) = delete;
    StateManager& operator=(const StateManager&) = delete;

    void changeState(IGameState* newState);
    void update();
    void draw();

private:
    StateManager() = default;

    IGameState* m_currentState = nullptr;
    IGameState* m_nextState = nullptr; 
};

#endif // STATE_MANAGER_H