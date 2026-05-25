#ifndef GAME32_PINS_H
#define GAME32_PINS_H

#include <driver/gpio.h>

// Onboard Diagnostics
#define LED_GPIO          GPIO_NUM_2

// Audio Subsystem
#define BUZZER_GPIO       GPIO_NUM_15

// I2C Display Interface (SSD1306)
#define TFT_SDA_GPIO      GPIO_NUM_21
#define TFT_SCL_GPIO      GPIO_NUM_22

// SPI SD Card Interface
#define PIN_NUM_MOSI      GPIO_NUM_23
#define PIN_NUM_MISO      GPIO_NUM_19
#define PIN_NUM_CLK       GPIO_NUM_18
#define PIN_NUM_CS        GPIO_NUM_4

// Input Button Matrix
#define BTN_UP            GPIO_NUM_32
#define BTN_DOWN          GPIO_NUM_33
#define BTN_LEFT          GPIO_NUM_25
#define BTN_RIGHT         GPIO_NUM_26
#define BTN_A             GPIO_NUM_27
#define BTN_B             GPIO_NUM_14
#define BTN_START         GPIO_NUM_17
#define BTN_SELECT        GPIO_NUM_13

// Analog Battery Monitoring
#define BATTERY_ADC_PIN   GPIO_NUM_34 // Maps to ADC1_CHANNEL_6

#endif // GAME32_PINS_H