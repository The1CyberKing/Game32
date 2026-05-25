#ifndef STARTUP_STATE_H
#define STARTUP_STATE_H

#include "../IGameState.h"
#include <stdint.h>

class StartupState : public IGameState {
public:
    static StartupState& getInstance(); // Singleton instance provider
    
    void onEnter() override;
    void onUpdate() override;
    void onDraw() override;
    void onExit() override;

private:
    StartupState() = default;
    uint32_t m_frameCount = 0;
};

#endif // STARTUP_STATE_H