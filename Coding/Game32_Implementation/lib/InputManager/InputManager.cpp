#include "InputManager.h"

static const gpio_num_t button_pins[] = {
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, 
    BTN_A, BTN_B, BTN_START, BTN_SELECT
};
#define NUM_BUTTONS (sizeof(button_pins) / sizeof(button_pins[0]))

InputManager& InputManager::getInstance() {
    static InputManager instance;
    return instance;
}

void InputManager::initialize() {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_reset_pin(button_pins[i]);
        gpio_set_direction(button_pins[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(button_pins[i], GPIO_PULLUP_ONLY);
    }
}

void InputManager::update() {
    m_lastState = m_currentState;
    m_currentState = 0;

    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (gpio_get_level(button_pins[i]) == 0) {
            m_currentState |= (1 << i);
        }
    }
}

bool InputManager::isHeld(uint8_t btnIndex) {
    return (m_currentState & (1 << btnIndex)) != 0;
}

bool InputManager::justPressed(uint8_t btnIndex) {
    bool current = (m_currentState & (1 << btnIndex)) != 0;
    bool last = (m_lastState & (1 << btnIndex)) != 0;
    return current && !last;
}