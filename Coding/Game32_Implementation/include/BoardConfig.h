#pragma once

// ==========================================
// Game32 Hardware Configuration
// ==========================================

// --- Feature Flags ---
#define GAME32_HAS_SD_CARD true
#define GAME32_HAS_WIFI true
#define GAME32_HAS_AUDIO true
#define GAME32_DISPLAY_WIDTH 128
#define GAME32_DISPLAY_HEIGHT 64

// --- Status & Power ---
#define LED_GPIO 2
#define BAT_ADC_GPIO 34 // ADC1_CHANNEL_6

// --- Input Buttons ---
#define BTN_UP_GPIO     32
#define BTN_DOWN_GPIO   33
#define BTN_LEFT_GPIO   25
#define BTN_RIGHT_GPIO  26
#define BTN_A_GPIO      27
#define BTN_B_GPIO      14
#define BTN_START_GPIO  17
#define BTN_SELECT_GPIO 13

// --- Audio ---
#define BUZZER_GPIO 15

// --- SD Card SPI Pins ---
#define SD_MOSI_GPIO 23
#define SD_MISO_GPIO 19
#define SD_CLK_GPIO  18
#define SD_CS_GPIO   4

// --- OLED I2C Pins ---
#define TFT_SDA_GPIO 21
#define TFT_SCL_GPIO 22