#ifndef GAME32_TYPES_H
#define GAME32_TYPES_H

#include <stdint.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Operating System State Machine Enums
enum class SystemState : uint8_t {
    Uninitialized,
    Startup,
    MainMenu,
    EmulatorRunning,
    SystemSleep
};

// Global Shared Atomic Context for Thread-Safe, Cross-Core Communication
struct SystemContext {
    std::atomic<SystemState> current_state{SystemState::Uninitialized};
    std::atomic<uint32_t>    active_pixel_count{6900}; 
    std::atomic<uint16_t>    battery_percentage_tenths{1000}; 
};

extern SystemContext sysContext;

// Hardware Bus Synchronization Mutexes
extern SemaphoreHandle_t g_i2cMutex;

#endif // GAME32_TYPES_H