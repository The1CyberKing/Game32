#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "config.h"
#include "pins.h"
#include <esp_err.h>
#include <stdbool.h>

class DisplayManager {
public:
    static DisplayManager& getInstance();
    DisplayManager(const DisplayManager&) = delete;
    DisplayManager& operator=(const DisplayManager&) = delete;

    esp_err_t initialize();
    void clearBuffer();
    void drawPixel(int16_t x, int16_t y, uint8_t color);
    void drawChar(int16_t x, int16_t y, char c);
    void drawString(int16_t x, int16_t y, const char* str);
    
    esp_err_t renderPipelinePush();

private:
    DisplayManager() = default;
    esp_err_t sendCommand(uint8_t command);

    uint8_t m_frameBuffer[1024] = {0}; 
    bool m_dirtyPages[8] = {true, true, true, true, true, true, true, true}; // Track changed lines
    uint32_t m_cachedPixelCount = 0; 
};

#endif // DISPLAY_MANAGER_H