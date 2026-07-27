#ifndef OVERLAY_STATE_H
#define OVERLAY_STATE_H

#include <cstdint>

class OverlayState {
public:
    static OverlayState& getInstance();
    
    void onEnter();
    void onUpdate();
    void onDraw();
    void onExit();

    bool isActive() const { return m_active; }
    void resume() { m_active = false; }

private:
    OverlayState() = default;
    
    bool m_active{false};
    uint32_t m_beepEndTimeMs{0};
    uint8_t m_savedVolume{3};
    bool m_isMuted{false};
    
    int m_cursorIndex{0};
    static constexpr int NUM_ITEMS = 6;
};

#endif // OVERLAY_STATE_H
