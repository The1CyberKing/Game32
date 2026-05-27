#pragma once
struct InputManager {
    static InputManager& getInstance() { static InputManager i; return i; }
    bool justPressed(int b) { return false; }
    bool isPressed(int b) { return false; }
    bool isHeld(int b) { return false; }
};
