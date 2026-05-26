#include "DisplayManager.h"
#include "types.h"
#include <driver/i2c.h>
#include <string.h>

static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // 0: Space
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 1: 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 2: 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 3: 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 4: 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 5: 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 6: 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 7: 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 8: 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 9: 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 10: 9
    {0x7C, 0x12, 0x11, 0x12, 0x7C}, // 11: A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 12: B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 13: C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 14: D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 15: E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 16: F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 17: G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 18: H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 19: I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 20: J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 21: K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 22: L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 23: M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 24: N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 25: O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 26: P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 27: Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 28: R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 29: S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 30: T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 31: U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 32: V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 33: W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 34: X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 35: Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 36: Z
    {0x00, 0x60, 0x60, 0x00, 0x00}  // 37: Period (.)
};

DisplayManager& DisplayManager::getInstance() {
    static DisplayManager instance;
    return instance;
}

esp_err_t DisplayManager::sendCommandLocked(uint8_t command) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SSD1306_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true);  
    i2c_master_write_byte(cmd, command, true);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(cmd);
    
    return ret;
}

esp_err_t DisplayManager::sendCommand(uint8_t command) {
    if (xSemaphoreTake(g_i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        esp_err_t ret = sendCommandLocked(command);
        xSemaphoreGive(g_i2cMutex);
        return ret;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t DisplayManager::initialize() {
    static bool isInitialized = false;
    if (isInitialized) return ESP_OK; // Gentle bypass if already running

    // C++ Compliant Initialization
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = TFT_SDA_GPIO;
    conf.scl_io_num = TFT_SCL_GPIO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_FREQ_HZ; 

    i2c_param_config(I2C_MASTER_NUM, &conf);
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));

    sendCommand(0xAE); 
    sendCommand(0xD5); sendCommand(0x80);
    sendCommand(0xA8); sendCommand(0x3F); 
    sendCommand(0xD3); sendCommand(0x00);
    sendCommand(0x40); 
    sendCommand(0x8D); sendCommand(0x14); 
    sendCommand(0x20); sendCommand(0x02); // Page Addressing Mode
    sendCommand(0xA1); sendCommand(0xC8); 
    sendCommand(0xDA); sendCommand(0x12);
    sendCommand(0x81); sendCommand(0xFF); 
    sendCommand(0xD9); sendCommand(0xF1);
    sendCommand(0xDB); sendCommand(0x40);
    sendCommand(0xA4); sendCommand(0xA6); 
    
    isInitialized = true;
    return sendCommand(0xAF);
}

void DisplayManager::clearBuffer() {
    memset(m_frameBuffer, 0, sizeof(m_frameBuffer));
    memset(m_dirtyPages, true, sizeof(m_dirtyPages)); 
    m_cachedPixelCount.store(0); 
}

void DisplayManager::drawPixel(int16_t x, int16_t y, uint8_t color) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    
    uint8_t page = y / 8;
    uint16_t buffer_idx = x + (page * 128);
    uint8_t bit_mask = (1 << (y & 7));
    bool is_already_set = (m_frameBuffer[buffer_idx] & bit_mask) != 0;

    if (color) {
        if (!is_already_set) {
            m_frameBuffer[buffer_idx] |= bit_mask;
            m_cachedPixelCount++; 
            m_dirtyPages[page] = true; 
        }
    } else {
        if (is_already_set) {
            m_frameBuffer[buffer_idx] &= ~bit_mask;
            m_cachedPixelCount--; 
            m_dirtyPages[page] = true; 
        }
    }
}

void DisplayManager::drawChar(int16_t x, int16_t y, char c, uint8_t color, uint8_t bg_color) {
    uint8_t lookup_idx = 0;
    
    if (c >= 'a' && c <= 'z') c -= 32; 

    if (c >= '0' && c <= '9') {
        lookup_idx = c - '0' + 1;
    } else if (c >= 'A' && c <= 'Z') {
        lookup_idx = c - 'A' + 11;
    } else if (c == '.') {       
        lookup_idx = 37;
    } else {
        lookup_idx = 0; 
    }

    for (int i = 0; i < 5; i++) {
        uint8_t line = font5x7[lookup_idx][i];
        for (int j = 0; j < 7; j++) {
            if (line & (1 << j)) {
                drawPixel(x + i, y + j, color);
            } else if (bg_color != 2) {
                drawPixel(x + i, y + j, bg_color);
            }
        }
    }
}

void DisplayManager::drawString(int16_t x, int16_t y, const char* str, uint8_t color, uint8_t bg_color) {
    while (*str) {
        if (x > 122) break; 
        drawChar(x, y, *str, color, bg_color);
        x += 6; 
        str++;
    }
}

void DisplayManager::drawText(int16_t x, int16_t y, const char* str, uint8_t color, uint8_t bg_color) {
    drawString(x, y, str, color, bg_color);
}

void DisplayManager::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    for (int16_t i = x; i < x + w; i++) {
        for (int16_t j = y; j < y + h; j++) {
            drawPixel(i, j, color);
        }
    }
}

void DisplayManager::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color) {
    int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy, e2;

    while (true) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

esp_err_t DisplayManager::renderPipelinePush() {
    sysContext.active_pixel_count.store(m_cachedPixelCount.load());

    for (int page = 0; page < 8; page++) {
        if (!m_dirtyPages[page]) continue; 

        // Grab the Mutex here safely inside the loop
        if (xSemaphoreTake(g_i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            
            // --- Phase 1: Set the Coordinates ---
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (SSD1306_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
            i2c_master_write_byte(cmd, 0x00, true);         // Command Stream
            i2c_master_write_byte(cmd, 0xB0 + page, true); // Target Page
            i2c_master_write_byte(cmd, 0x00, true);        // Column Low
            i2c_master_write_byte(cmd, 0x10, true);        // Column High
            
            // --- Phase 2: Send the 128 Bytes of Data ---
            i2c_master_start(cmd); // Fast Restart
            i2c_master_write_byte(cmd, (SSD1306_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
            i2c_master_write_byte(cmd, 0x40, true);        // Data Stream
            i2c_master_write(cmd, &m_frameBuffer[page * 128], 128, true);
            i2c_master_stop(cmd);
            
            // Fire the entire bundle at once
            i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(20));
            i2c_cmd_link_delete(cmd);

            m_dirtyPages[page] = false; 
            xSemaphoreGive(g_i2cMutex);
        }
    }
    return ESP_OK;
}

void DisplayManager::sleepDisplay() {
    sendCommand(0xAE); // Display Off
}

void DisplayManager::wakeDisplay() {
    sendCommand(0xAF); // Display On
}