#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include "BoardConfig.h"
#include <stdint.h>

class InputManager {
public:
    static InputManager& getInstance();
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    void initialize();
    void update(); // Reads and debounces state

    bool isHeld(uint8_t btnIndex);
    bool justPressed(uint8_t btnIndex);

private:
    InputManager() = default;

    uint8_t m_currentState = 0;
    uint8_t m_lastState = 0;
};

#endif // INPUT_MANAGER_H