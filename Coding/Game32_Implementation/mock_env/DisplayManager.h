#pragma once
#include <cstdint>
struct DisplayManager {
    static DisplayManager& getInstance() { static DisplayManager d; return d; }
    void drawText(int x, int y, const char* t) {}
    void renderPipelinePush() {}
    void drawArduboyFrame(const uint8_t* b) {}
    void clearBuffer() {}
};
