#include "StateManager.h"

StateManager& StateManager::getInstance() {
    static StateManager instance;
    return instance;
}

void StateManager::changeState(IGameState* newState) {
    m_nextState = newState;
}

void StateManager::update() {
    if (m_nextState) {
        if (m_currentState) m_currentState->onExit();
        
        m_currentState = m_nextState; 
        m_nextState = nullptr;
        m_currentState->onEnter();
    }

    if (m_currentState) m_currentState->onUpdate();
}

void StateManager::draw() {
    if (m_currentState) m_currentState->onDraw();
}