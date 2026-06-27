#include "BatteryManager.h"
#include "types.h"
#include <algorithm>
#include <rom/ets_sys.h> // Required for microsecond hardware delays
#include "esp_adc/adc_oneshot.h"

static adc_oneshot_unit_handle_t adc1_handle = nullptr;

BatteryManager& BatteryManager::getInstance() {
    static BatteryManager instance;
    return instance;
}

void BatteryManager::initialize() {
    adc_oneshot_unit_init_cfg_t init_config = {};
    init_config.unit_id = ADC_UNIT_1;
    init_config.ulp_mode = ADC_ULP_MODE_DISABLE;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {};
    config.atten = ADC_ATTEN_DB_12;
    config.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &config));
}

void BatteryManager::updateService() {
    if (adc1_handle == nullptr) return;

    for (uint8_t i = 0; i < BATTERY_SAMPLE_COUNT; ++i) {
        int raw_val = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &raw_val));
        m_samples[i] = raw_val;
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

uint16_t BatteryManager::getBatteryPercentage() {
    return sysContext.battery_percentage_tenths.load() / 10;
}