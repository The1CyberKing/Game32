#ifndef GAME32_CONFIG_H
#define GAME32_CONFIG_H

#include <stdint.h>
#include <driver/i2c.h>

// I2C Bus Configuration
#define I2C_MASTER_NUM           I2C_NUM_0
constexpr uint32_t I2C_FREQ_HZ   = 400000;
constexpr uint8_t  SSD1306_I2C_ADDR = 0x3C;

// Power Management Configuration
constexpr uint32_t PIXEL_SAFE_THRESHOLD  = 4000;
constexpr uint32_t PIXEL_MIN_CRITICAL    = 1000;
constexpr uint32_t IDLE_TIMEOUT_MS       = 10000;

// Battery Filter Constants
constexpr uint8_t  BATTERY_SAMPLE_COUNT  = 30;
constexpr uint8_t  BATTERY_OUTLIER_DROP  = 5;

// FreeRTOS Task Allocations
constexpr uint32_t DEFAULTS_STACK_SIZE_CORE0 = 4096;
constexpr uint32_t GRAPHICS_STACK_SIZE_CORE1 = 8192;

// File System Path Conventions
#define SD_SYS_DIR   "/sd/sys"
#define SD_GAMES_DIR "/sd/games"
#define SD_SAVES_DIR "/sd/saves"
#define SD_ASSET_DIR "/sd/assets"

#endif // GAME32_CONFIG_H