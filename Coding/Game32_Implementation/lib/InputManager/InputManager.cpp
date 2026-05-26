#include "InputManager.h"
#include <driver/gpio.h>

static const gpio_num_t button_pins[] = {
    (gpio_num_t)BTN_UP_GPIO, (gpio_num_t)BTN_DOWN_GPIO, (gpio_num_t)BTN_LEFT_GPIO, (gpio_num_t)BTN_RIGHT_GPIO, 
    (gpio_num_t)BTN_A_GPIO, (gpio_num_t)BTN_B_GPIO, (gpio_num_t)BTN_START_GPIO, (gpio_num_t)BTN_SELECT_GPIO
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