#ifndef OVERLAY_STATE_H
#define OVERLAY_STATE_H

#include "../IGameState.h"
#include <cstdint>

class OverlayState : public IGameState {
public:
    static OverlayState& getInstance();
    
    void onEnter() override;
    void onUpdate() override;
    void onDraw() override;
    void onExit() override;

private:
    OverlayState() = default;
    
    uint32_t m_beepEndTimeMs{0};
    uint8_t m_savedVolume{3};
    bool m_isMuted{false};
    
    int m_cursorIndex{0};
    static constexpr int NUM_ITEMS = 6;
};

#endif // OVERLAY_STATE_H
