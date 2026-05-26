#include "PowerManager.h"
#include "types.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>

PowerManager& PowerManager::getInstance() {
    static PowerManager instance;
    return instance;
}

void PowerManager::initialize() {
    gpio_reset_pin((gpio_num_t)LED_GPIO);
    gpio_set_direction((gpio_num_t)LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)LED_GPIO, 0);
}

void PowerManager::executeAdaptiveKeepalive() {
    uint32_t pixel_density = sysContext.active_pixel_count.load();

    if (pixel_density >= PIXEL_SAFE_THRESHOLD) {
        gpio_set_level((gpio_num_t)LED_GPIO, 0); 
        return;
    }

    uint32_t deficit = PIXEL_SAFE_THRESHOLD - pixel_density;
    uint32_t max_deficit = PIXEL_SAFE_THRESHOLD - PIXEL_MIN_CRITICAL;
    
    float intensity = static_cast<float>(deficit) / static_cast<float>(max_deficit);
    if (intensity > 1.0f) intensity = 1.0f;

    uint32_t target_loops = static_cast<uint32_t>(intensity * 40000);

    if (target_loops > 0) {
        gpio_set_level((gpio_num_t)LED_GPIO, 1); 
        
        volatile uint32_t furnace = 0xACE1U; 
        for (uint32_t i = 0; i < target_loops; ++i) {
            furnace ^= (furnace << 13);
            furnace ^= (furnace >> 17);
            furnace ^= (furnace << 5);

            if ((i & 0x1FFF) == 0) {
                // FIX: Force a context switch to guarantee watchdog feeding
                vTaskDelay(pdMS_TO_TICKS(1)); 
            }
        }
    }
}