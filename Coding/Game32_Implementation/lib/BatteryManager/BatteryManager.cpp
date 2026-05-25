#include "BatteryManager.h"
#include "types.h"
#include <driver/adc.h>
#include <algorithm>
#include <rom/ets_sys.h> // Required for microsecond hardware delays

BatteryManager& BatteryManager::getInstance() {
    static BatteryManager instance;
    return instance;
}

void BatteryManager::initialize() {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
}

void BatteryManager::updateService() {
    for (uint8_t i = 0; i < BATTERY_SAMPLE_COUNT; ++i) {
        m_samples[i] = adc1_get_raw(ADC1_CHANNEL_6);
        ets_delay_us(250); // FIX: Hardware micro-delay separates sample noise
    }

    std::sort(m_samples, m_samples + BATTERY_SAMPLE_COUNT);

    uint32_t accumulator = 0;
    constexpr uint8_t start_index = BATTERY_OUTLIER_DROP;
    constexpr uint8_t end_index = BATTERY_SAMPLE_COUNT - BATTERY_OUTLIER_DROP;
    constexpr uint8_t valid_sample_count = end_index - start_index;

    for (uint8_t i = start_index; i < end_index; ++i) {
        accumulator += m_samples[i];
    }
    
    uint32_t smoothed_raw = accumulator / valid_sample_count;
    float calculated_voltage = (smoothed_raw / 4095.0f) * 3.3f * 2.0f; 
    float percentage = ((calculated_voltage - 3.4f) / (4.2f - 3.4f)) * 100.0f;

    if (percentage > 100.0f) percentage = 100.0f;
    if (percentage < 0.0f)   percentage = 0.0f;

    uint16_t percentage_tenths = static_cast<uint16_t>(percentage * 10.0f);
    sysContext.battery_percentage_tenths.store(percentage_tenths);
}