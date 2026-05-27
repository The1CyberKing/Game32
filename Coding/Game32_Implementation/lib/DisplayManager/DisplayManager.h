#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "config.h"
#include "BoardConfig.h"
#include <esp_err.h>
#include <stdbool.h>
#include <atomic>

class DisplayManager {
public:
    static DisplayManager& getInstance();
    DisplayManager(const DisplayManager&) = delete;
    DisplayManager& operator=(const DisplayManager&) = delete;

    esp_err_t initialize();
    void clearBuffer();
    void drawPixel(int16_t x, int16_t y, uint8_t color);
    void drawChar(int16_t x, int16_t y, char c, uint8_t color = 1, uint8_t bg_color = 2);
    void drawString(int16_t x, int16_t y, const char* str, uint8_t color = 1, uint8_t bg_color = 2);
    void drawText(int16_t x, int16_t y, const char* str, uint8_t color = 1, uint8_t bg_color = 2);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color);
    
    esp_err_t renderPipelinePush();
    void drawArduboyFrame(const uint8_t* buffer);
    void sleepDisplay();
    void wakeDisplay();

private:
    DisplayManager() = default;
    esp_err_t sendCommand(uint8_t command);
    esp_err_t sendCommandLocked(uint8_t command);

    uint8_t m_frameBuffer[1024] = {0}; 
    bool m_dirtyPages[8] = {true, true, true, true, true, true, true, true}; // Track changed lines
    std::atomic<uint32_t> m_cachedPixelCount{0}; 
};

#endif // DISPLAY_MANAGER_H