#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include "config.h"
#include "BoardConfig.h"

class BatteryManager {
public:
    static BatteryManager& getInstance();
    BatteryManager(const BatteryManager&) = delete;
    BatteryManager& operator=(const BatteryManager&) = delete;

    void initialize();
    void updateService();
    uint16_t getBatteryPercentage();

private:
    BatteryManager() = default; 
    uint32_t m_samples[BATTERY_SAMPLE_COUNT] = {0}; // Zero-heap stack array
};

#endif // BATTERY_MANAGER_H