#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"


// Pin definitions
#define LED_GPIO 2
#define VOLTAGE_ADC ADC_CHANNEL_6  // GPIO34 on ESP32 WROOM32

// Button pins
#define BTN_UP      32
#define BTN_DOWN    33
#define BTN_LEFT    25
#define BTN_RIGHT   26
#define BTN_A       27
#define BTN_B       14
#define BTN_START   17
#define BTN_SELECT  13

#define BUZZER_GPIO 15

// SD card pins
#define PIN_NUM_MOSI 23
#define PIN_NUM_MISO 19
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   4

// TFT Display pins (I2C)
#define TFT_SDA_GPIO 21
#define TFT_SCL_GPIO 22
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000

// SSD1306 OLED I2C address
#define SSD1306_I2C_ADDR 0x3C

// Battery voltage thresholds for 3.7V Li-ion (103665 cell)
#define BATTERY_FULL_VOLTAGE  4.20f   // Fully charged
#define BATTERY_CUTOFF_VOLTAGE  3.00f   // Critical low (protect battery)
#define BATTERY_EMPTY_VOLTAGE  3.30f   // Low battery warning

static const char *TAG = "Game32";
static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_handle = NULL;

#define ADC_SAMPLES 64

// Button GPIO array
static const gpio_num_t button_pins[] = {
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, 
    BTN_A, BTN_B, BTN_START, BTN_SELECT
};

static const char* button_names[] = {
    "UP", "DOWN", "LEFT", "RIGHT",
    "A", "B", "START", "SELECT"
};

// Different frequencies for each button (in Hz)
static const uint32_t button_frequencies[] = {
    262,  // UP - C4
    294,  // DOWN - D4
    330,  // LEFT - E4
    349,  // RIGHT - F4
    392,  // A - G4
    440,  // B - A4
    494,  // START - B4
    523   // SELECT - C5
};

#define NUM_BUTTONS (sizeof(button_pins) / sizeof(button_pins[0]))

// ===== I2C / TFT DISPLAY FUNCTIONS =====
static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TFT_SDA_GPIO,
        .scl_io_num = TFT_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        return err;
    }
    
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static esp_err_t ssd1306_write_command(uint8_t command)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SSD1306_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true);  // Command mode
    i2c_master_write_byte(cmd, command, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t ssd1306_set_contrast(uint8_t contrast)
{
    esp_err_t ret;
    ret = ssd1306_write_command(0x81);  // Set contrast command
    if (ret != ESP_OK) return ret;
    ret = ssd1306_write_command(contrast);  // Contrast value (0x00 to 0xFF)
    return ret;
}

static esp_err_t ssd1306_init(void)
{
    esp_err_t ret;
    
    // Initialization sequence for SSD1306
    ret = ssd1306_write_command(0xAE); // Display OFF
    if (ret != ESP_OK) return ret;
    
    ret = ssd1306_write_command(0xD5); // Set display clock divide ratio
    ret = ssd1306_write_command(0x80);
    
    ret = ssd1306_write_command(0xA8); // Set multiplex ratio
    ret = ssd1306_write_command(0x3F); // 1/64 duty
    
    ret = ssd1306_write_command(0xD3); // Set display offset
    ret = ssd1306_write_command(0x00);
    
    ret = ssd1306_write_command(0x40); // Set start line
    
    ret = ssd1306_write_command(0x8D); // Charge pump
    ret = ssd1306_write_command(0x14); // Enable charge pump
    
    ret = ssd1306_write_command(0x20); // Memory addressing mode
    ret = ssd1306_write_command(0x00); // Horizontal addressing mode
    
    ret = ssd1306_write_command(0xA1); // Set segment remap (flip horizontally)
    ret = ssd1306_write_command(0xC8); // Set COM output scan direction (flip vertically)
    
    ret = ssd1306_write_command(0xDA); // Set COM pins hardware configuration
    ret = ssd1306_write_command(0x12);
    
    ret = ssd1306_write_command(0x81); // Set contrast
    ret = ssd1306_write_command(0xFF); // Start at max brightness
    
    ret = ssd1306_write_command(0xD9); // Set pre-charge period
    ret = ssd1306_write_command(0xF1);
    
    ret = ssd1306_write_command(0xDB); // Set VCOMH deselect level
    ret = ssd1306_write_command(0x40);
    
    ret = ssd1306_write_command(0xA4); // Display all on resume
    ret = ssd1306_write_command(0xA6); // Normal display (not inverted)
    
    ret = ssd1306_write_command(0xAF); // Display ON
    
    return ret;
}

static esp_err_t ssd1306_clear(void)
{
    uint8_t clear_data[128] = {0};
    
    for (int page = 0; page < 8; page++) {
        ssd1306_write_command(0xB0 + page); // Set page address
        ssd1306_write_command(0x00);        // Set lower column address
        ssd1306_write_command(0x10);        // Set higher column address
        
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (SSD1306_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, 0x40, true);  // Data mode
        i2c_master_write(cmd, clear_data, 128, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
        i2c_cmd_link_delete(cmd);
        
        if (ret != ESP_OK) return ret;
    }
    
    return ESP_OK;
}

static esp_err_t ssd1306_test_pattern(void)
{
    // 'Spotify' logo, 128x64, SSD1306 page format
    static const uint8_t spotify_logo[1024] = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xc0, 0xc0, 0xe0, 
0xe0, 0xf0, 0xf8, 0xf8, 0xf8, 0xfc, 0xfc, 0xfc, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 
0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfc, 0xfc, 0xfc, 0xf8, 0xf8, 0xf8, 0xf0, 0xf0, 
0xe0, 0xc0, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0xe0, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x07, 0x07, 0x03, 0x03, 0x03, 
0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 
0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x03, 0x03, 0x03, 0x03, 0x03, 0x07, 0x07, 0x07, 0x0f, 0x0f, 
0x0f, 0x1f, 0x1f, 0x3f, 0x3f, 0x7f, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0xe0, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x3e, 0x3e, 0x1e, 0x1e, 
0x1f, 0x1f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 
0x0f, 0x0f, 0x0f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1e, 0x3e, 0x3e, 0x3e, 0x7c, 0x7c, 0xfc, 0xf8, 0xf8, 
0xf8, 0xf0, 0xf0, 0xe0, 0xe0, 0xe0, 0xf0, 0xf9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0xf8, 0xf8, 0x78, 
0x7c, 0x7c, 0x7c, 0x7c, 0x3c, 0x3c, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 
0x3c, 0x7c, 0x7c, 0x7c, 0x7c, 0x78, 0xf8, 0xf8, 0xf0, 0xf0, 0xf0, 0xe0, 0xe0, 0xc0, 0xc0, 0x80, 
0x81, 0x83, 0xc3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x07, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf9, 0xf0, 0xf0, 
0xf0, 0xf0, 0xf0, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 
0xf8, 0xf8, 0xf8, 0xf0, 0xf0, 0xf0, 0xe0, 0xe0, 0xe0, 0xc1, 0xc1, 0x83, 0x83, 0x87, 0x87, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x07, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x3f, 0x1f, 0x0f, 0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x03, 0x07, 
0x0f, 0x0f, 0x1f, 0x1f, 0x1f, 0x3f, 0x3f, 0x3f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0xff, 
0xff, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x3f, 0x3f, 0x3f, 0x1f, 0x1f, 0x1f, 0x0f, 0x0f, 
0x07, 0x03, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    for (int page = 0; page < 8; page++) {
        ssd1306_write_command(0xB0 + page); // Set page address
        ssd1306_write_command(0x00);        // Set lower column address
        ssd1306_write_command(0x10);        // Set higher column address

        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (SSD1306_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, 0x40, true);  // Data mode
        i2c_master_write(cmd, &spotify_logo[page * 128], 128, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
        i2c_cmd_link_delete(cmd);

        if (ret != ESP_OK) return ret;
    }

    return ESP_OK;
}


static bool tft_init_and_test(void)
{
    ESP_LOGI(TAG, "Initializing I2C for TFT display...");
    
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "I2C initialized successfully");
    
    // Try to detect device
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SSD1306_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "TFT display not detected at address 0x%02X", SSD1306_I2C_ADDR);
        return false;
    }
    
    ESP_LOGI(TAG, "TFT display detected at address 0x%02X", SSD1306_I2C_ADDR);
    
    // Initialize display
    ret = ssd1306_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SSD1306: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "SSD1306 initialized successfully");
    
    // Clear display
    ret = ssd1306_clear();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear display");
        return false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Draw test pattern
    ESP_LOGI(TAG, "Drawing test pattern...");
    ret = ssd1306_test_pattern();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to draw test pattern");
        return false;
    }
    
    ESP_LOGI(TAG, "TFT test pattern displayed");
    
// Comprehensive brightness test
ESP_LOGI(TAG, "Testing brightness control methods...");

// Method 1: Contrast
ESP_LOGI(TAG, "Method 1: Testing Contrast (0x81)");
for (int contrast = 255; contrast >= 0; contrast -= 4) {
    ssd1306_set_contrast(contrast);
    ESP_LOGI(TAG, "  Contrast: %d/255", contrast);
    vTaskDelay(pdMS_TO_TICKS(100));
}

// Reset
ssd1306_set_contrast(255);
vTaskDelay(pdMS_TO_TICKS(1000));

ESP_LOGI(TAG, "Brightness test complete - reset to defaults");
    
    return true;
}

// ===== BUZZER FUNCTIONS =====
static void buzzer_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 2000,  // Default frequency
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);
    
    ESP_LOGI(TAG, "Buzzer initialized on GPIO %d", BUZZER_GPIO);
}

static void buzzer_beep(uint32_t frequency, uint32_t duration_ms)
{
    // Set the frequency
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, frequency);
    
    // Turn on buzzer (50% duty cycle)
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    
    // Turn off buzzer
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// ===== BUTTON FUNCTIONS =====
static void buttons_init(void)
{
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_reset_pin(button_pins[i]);
        gpio_set_direction(button_pins[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(button_pins[i], GPIO_PULLUP_ONLY);
    }
    ESP_LOGI(TAG, "Buttons initialized");
}

// ===== ADC / BATTERY FUNCTIONS =====
static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc1_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, VOLTAGE_ADC, &chan_cfg));

    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
    };

    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &adc1_cali_handle) == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration enabled");
    } else {
        ESP_LOGW(TAG, "ADC calibration NOT available");
    }
}

static float read_battery_voltage(void)
{
    int raw;
    int voltage_mv;
    int sum_mv = 0;

    for (int i = 0; i < ADC_SAMPLES; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, VOLTAGE_ADC, &raw));

        if (adc1_cali_handle) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, raw, &voltage_mv));
        } else {
            voltage_mv = (raw * 3300) / 4095;
        }

        sum_mv += voltage_mv;
    }

    float avg_mv = sum_mv / (float)ADC_SAMPLES;

    // Voltage divider: 100k / 100k → x2
    return (avg_mv / 1000.0f) * 2.0f;
}

static int calculate_battery_percent(float voltage)
{
    const float curve[][2] = {
        {4.20, 100},
        {4.00, 85},
        {3.90, 75},
        {3.80, 60},
        {3.70, 45},
        {3.60, 30},
        {3.50, 15},
        {3.40, 5},
        {3.30, 0},
    };

    if (voltage >= 4.2f) return 100;
    if (voltage <= 3.3f) return 0;

    for (int i = 0; i < 8; i++) {
        if (voltage <= curve[i][0] && voltage > curve[i + 1][0]) {
            float v1 = curve[i][0];
            float p1 = curve[i][1];
            float v2 = curve[i + 1][0];
            float p2 = curve[i + 1][1];

            return (int)(p1 + (voltage - v1) * (p2 - p1) / (v2 - v1));
        }
    }

    return 0;
}

// ===== SD CARD FUNCTIONS =====
static bool sdcard_detect(void)
{
    esp_err_t ret;
    sdmmc_card_t *card;
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000
    };
    
    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return false;
    }
    
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = SPI2_HOST;
    
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return false;
    }
    
    ESP_LOGI(TAG, "SD card mounted successfully");
    sdmmc_card_print_info(stdout, card);
    
    return true;
}

// ===== LED BLINK TASK =====
void blink_task(void *arg)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    
    while (1) {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ===== BUTTON MONITOR TASK =====
void button_monitor_task(void *arg)
{
    bool prev_state[NUM_BUTTONS] = {true};
    
    while (1) {
        for (int i = 0; i < NUM_BUTTONS; i++) {
            bool current_state = gpio_get_level(button_pins[i]);
            
            if (current_state == 0 && prev_state[i] == 1) {
                ESP_LOGI(TAG, "Button pressed: %s (%d Hz)", button_names[i], button_frequencies[i]);
                buzzer_beep(button_frequencies[i], 150);  // Play frequency for 150ms
            }
            
            prev_state[i] = current_state;
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ===== BATTERY MONITOR TASK =====
void battery_monitor_task(void *arg)
{
    while (1) {
        float voltage = read_battery_voltage();
        int percent = calculate_battery_percent(voltage);
        
        ESP_LOGI(TAG, "🔋 Battery: %.2fV (%d%%)", voltage, percent);
        
        if (percent <= 5) {
            ESP_LOGE(TAG, "🔴 CRITICAL BATTERY!");
        } else if (percent <= 10) {
            ESP_LOGW(TAG, "⚠️ LOW BATTERY");
        }

        
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}

// ===== MAIN =====
void app_main(void)
{
    // Reduce SD card error verbosity
    esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_WARN);
    
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "  Game32 Hardware Test v1.0");
    ESP_LOGI(TAG, "=================================");
    
    // Initialize hardware
    adc_init();
    buttons_init();
    buzzer_init();
    
    // Test buzzer with startup melody
    ESP_LOGI(TAG, "Testing buzzer with startup melody...");
    buzzer_beep(523, 100);  // C5
    vTaskDelay(pdMS_TO_TICKS(50));
    buzzer_beep(659, 100);  // E5
    vTaskDelay(pdMS_TO_TICKS(50));
    buzzer_beep(784, 150);  // G5
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Initialize and test TFT display (includes brightness test)
    if (tft_init_and_test()) {
        ESP_LOGI(TAG, "✓ TFT display working");
    } else {
        ESP_LOGW(TAG, "✗ TFT display not working or not connected");
    }
    
    // Check SD card
    if (sdcard_detect()) {
        ESP_LOGI(TAG, "✓ SD card detected and mounted");
    } else {
        ESP_LOGW(TAG, "✗ SD card not detected");
    }
    
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "Starting continuous monitoring...");
    ESP_LOGI(TAG, "Press buttons to hear different tones:");
    ESP_LOGI(TAG, "  UP=C4, DOWN=D4, LEFT=E4, RIGHT=F4");
    ESP_LOGI(TAG, "  A=G4, B=A4, START=B4, SELECT=C5");
    ESP_LOGI(TAG, "=================================");
    
    // Create tasks
    xTaskCreate(blink_task, "blink_task", 2048, NULL, 5, NULL);
    xTaskCreate(button_monitor_task, "button_task", 4096, NULL, 5, NULL);
    xTaskCreate(battery_monitor_task, "battery_task", 4096, NULL, 3, NULL);
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}