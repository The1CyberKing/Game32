#ifndef GAME_ENGINE_STATE_H
#define GAME_ENGINE_STATE_H

#include "../IGameState.h"
#include "IGame.h"
#include <cstdint>

class GameEngineState : public IGameState {
public:
    static GameEngineState& getInstance();
    
    void setTargetGame(IGame* game);
    IGame* getTargetGame() const { return m_activeGame; }
    
    void pauseToOverlay();
    void quitGame();
    
    void onEnter() override;
    void onUpdate() override;
    void onDraw() override;
    void onExit() override;

private:
    GameEngineState() = default;
    IGame* m_activeGame{nullptr};
    bool m_gameInitialized{false};
    bool m_isPausedToOverlay{false};
    uint64_t m_lastInputTime{0};
};

#endif // GAME_ENGINE_STATE_H
