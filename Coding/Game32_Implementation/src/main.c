#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>

#include "types.h"
#include "config.h"

#include "BatteryManager.h"
#include "PowerManager.h"
#include "DisplayManager.h"
#include "InputManager.h"

#include "StateManager.h"
#include "States/StartupState.h"

static const char* TAG = "Game32_Kernel";

// Global Context
SystemContext sysContext;
SemaphoreHandle_t g_i2cMutex = NULL;

// ===== CORE 0: SYSTEM HEALTH DAEMONS =====
void core0_system_services_task(void *pvParameters) {
    BatteryManager::getInstance().initialize();
    PowerManager::getInstance().initialize();

    while (1) {
        BatteryManager::getInstance().updateService();
        PowerManager::getInstance().executeAdaptiveKeepalive();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ===== CORE 1: MAIN APPLICATION ENGINE =====
void core1_graphics_engine_task(void *pvParameters) {
    DisplayManager::getInstance().initialize();
    InputManager::getInstance().initialize();

    StateManager::getInstance().changeState(&StartupState::getInstance());

    // FIX: Deterministic frame pacing
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frameDelay = pdMS_TO_TICKS(33); 

    while (1) {
        InputManager::getInstance().update(); // Poll & Debounce
        StateManager::getInstance().update();
        
        DisplayManager::getInstance().clearBuffer();
        StateManager::getInstance().draw();
        DisplayManager::getInstance().renderPipelinePush();

        // Pauses the task strictly for the exact remainder of the 33ms window
        vTaskDelayUntil(&lastWakeTime, frameDelay); 
    }
}

// ===== ENTRY POINT =====
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Initializing Game32 Kernel...");

    // FIX: Hardware Mutex MUST be created before any tasks start using the bus
    g_i2cMutex = xSemaphoreCreateMutex();
    if (g_i2cMutex == NULL) {
        ESP_LOGE(TAG, "FATAL: Failed to create I2C Mutex.");
        return;
    }

    xTaskCreatePinnedToCore(
        core0_system_services_task, "SystemServices", DEFAULTS_STACK_SIZE_CORE0, NULL, 2, NULL, 0
    );

    xTaskCreatePinnedToCore(
        core1_graphics_engine_task, "GraphicsEngine", GRAPHICS_STACK_SIZE_CORE1, NULL, 5, NULL, 1
    );
}