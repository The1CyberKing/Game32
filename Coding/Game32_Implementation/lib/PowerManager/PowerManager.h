#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "config.h"
#include "pins.h"

class PowerManager {
public:
    static PowerManager& getInstance();
    PowerManager(const PowerManager&) = delete;
    PowerManager& operator=(const PowerManager&) = delete;

    void initialize();
    void executeAdaptiveKeepalive();

private:
    PowerManager() = default;
};

#endif // POWER_MANAGER_H