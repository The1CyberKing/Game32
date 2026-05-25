#include "DisplayManager.h"
#include "types.h"
#include <driver/i2c.h>
#include <string.h>

// (Omitted font5x7 array here for brevity, keep the exact same array as before)

DisplayManager& DisplayManager::getInstance() {
    static DisplayManager instance;
    return instance;
}

esp_err_t DisplayManager::sendCommand(uint8_t command) {
    if (xSemaphoreTake(g_i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (SSD1306_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, 0x00, true);  
        i2c_master_write_byte(cmd, command, true);
        i2c_master_stop(cmd);
        
        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(10));
        i2c_cmd_link_delete(cmd);
        
        xSemaphoreGive(g_i2cMutex);
        return ret;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t DisplayManager::initialize() {
    // Prevent invalid state panics on soft resets
    i2c_driver_delete(I2C_MASTER_NUM); 

    // C++ Compliant Initialization (No chained designators)
    i2c_config_t conf = {}; 
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = TFT_SDA_GPIO;
    conf.scl_io_num = TFT_SCL_GPIO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_FREQ_HZ; 

    i2c_param_config(I2C_MASTER_NUM, &conf);
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));

    // SSD1306 Hardware Initialization Sequence
    sendCommand(0xAE); // Display Off
    sendCommand(0xD5); sendCommand(0x80);
    sendCommand(0xA8); sendCommand(0x3F); // 128x64 Mapped Height
    sendCommand(0xD3); sendCommand(0x00);
    sendCommand(0x40); // Set Start Line
    sendCommand(0x8D); sendCommand(0x14); // Enable Charge Pump
    sendCommand(0x20); sendCommand(0x00); // Horizontal Addressing Mode
    sendCommand(0xA1); sendCommand(0xC8); // Remap Flip Flop Left/Right
    sendCommand(0xDA); sendCommand(0x12);
    sendCommand(0x81); sendCommand(0xFF); // Max Out Contrast
    sendCommand(0xD9); sendCommand(0xF1);
    sendCommand(0xDB); sendCommand(0x40);
    sendCommand(0xA4); sendCommand(0xA6); // Normal Uninverted Display
    return sendCommand(0xAF); // Display On
}

void DisplayManager::clearBuffer() {
    memset(m_frameBuffer, 0, sizeof(m_frameBuffer));
    memset(m_dirtyPages, true, sizeof(m_dirtyPages)); // All pages flagged for wipe
    m_cachedPixelCount = 0; 
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
            m_dirtyPages[page] = true; // Mark segment as modified
        }
    } else {
        if (is_already_set) {
            m_frameBuffer[buffer_idx] &= ~bit_mask;
            m_cachedPixelCount--; 
            m_dirtyPages[page] = true; // Mark segment as modified
        }
    }
}

// (Omitted drawChar for brevity, exactly the same as prior)

void DisplayManager::drawString(int16_t x, int16_t y, const char* str) {
    while (*str) {
        if (x > 122) break; // FIX: Screen horizontal clipping boundary
        drawChar(x, y, *str);
        x += 6; 
        str++;
    }
}

esp_err_t DisplayManager::renderPipelinePush() {
    sysContext.active_pixel_count.store(m_cachedPixelCount);

    if (xSemaphoreTake(g_i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int page = 0; page < 8; page++) {
            if (!m_dirtyPages[page]) continue; // FIX: Skip pushing static pixels!

            sendCommand(0xB0 + page);
            sendCommand(0x00);
            sendCommand(0x10);

            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (SSD1306_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
            i2c_master_write_byte(cmd, 0x40, true);  
            i2c_master_write(cmd, &m_frameBuffer[page * 128], 128, true);
            i2c_master_stop(cmd);
            
            i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(10));
            i2c_cmd_link_delete(cmd);

            m_dirtyPages[page] = false; // Page is now clean
        }
        xSemaphoreGive(g_i2cMutex);
    }
    return ESP_OK;
}