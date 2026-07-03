#include "AudioEngine.h"
#include "BoardConfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>

static const char* TAG = "AudioEngine";

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          BUZZER_GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT // 10-bit resolution (0-1023)

AudioEngine& AudioEngine::getInstance() {
  static AudioEngine instance;
  return instance;
}

void AudioEngine::init() {
  if (m_initialized) return;

  ledc_timer_config_t ledc_timer = {};
  ledc_timer.speed_mode       = LEDC_MODE;
  ledc_timer.timer_num        = LEDC_TIMER;
  ledc_timer.duty_resolution  = LEDC_DUTY_RES;
  ledc_timer.freq_hz          = 1000;  // Default 1 kHz
  ledc_timer.clk_cfg          = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

  ledc_channel_config_t ledc_channel = {};
  ledc_channel.speed_mode     = LEDC_MODE;
  ledc_channel.channel        = LEDC_CHANNEL;
  ledc_channel.timer_sel      = LEDC_TIMER;
  ledc_channel.gpio_num       = LEDC_OUTPUT_IO;
  ledc_channel.duty           = 0; // Start muted
  ledc_channel.hpoint         = 0;
  ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

  m_initialized = true;
  ESP_LOGI(TAG, "AudioEngine initialized on GPIO %d", BUZZER_GPIO);
}

uint32_t AudioEngine::getDutyForVolume() {
  if (m_volume == 0) return 0;
  // Non-linear / exponential duty table for sub-microsecond pulse attenuation on passive buzzer
  static const uint16_t dutyTable[21] = {
    0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 320, 384, 448, 512
  };
  uint8_t idx = std::min((uint8_t)20, m_volume);
  return dutyTable[idx];
}

void AudioEngine::applyFrequency(uint16_t freq) {
  if (!m_initialized) return;
  if (freq == 0 || m_volume == 0) {
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
  } else {
    ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, getDutyForVolume());
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
  }
}

void AudioEngine::playTone(uint16_t freq1, uint16_t freq2) {
  m_freq1 = freq1;
  m_freq2 = freq2;
  
  if (m_freq1 == 0 && m_freq2 == 0) {
    stopTone();
    return;
  }

  applyFrequency(m_freq1 > 0 ? m_freq1 : m_freq2);
}

void AudioEngine::playTone1(uint16_t freq) {
  m_freq1 = freq;
  if (m_freq1 == 0 && m_freq2 == 0) {
    stopTone();
  } else if (m_freq1 > 0) {
    applyFrequency(m_freq1);
  }
}

void AudioEngine::playTone2(uint16_t freq) {
  m_freq2 = freq;
  if (m_freq1 == 0 && m_freq2 == 0) {
    stopTone();
  } else if (m_freq2 > 0 && m_freq1 == 0) {
    applyFrequency(m_freq2);
  }
}

void AudioEngine::stopTone() {
  m_freq1 = 0;
  m_freq2 = 0;
  applyFrequency(0);
}

void AudioEngine::update() {
  if (!m_initialized) return;
  if (m_freq1 == 0 && m_freq2 == 0) return;

  if (m_freq1 > 0 && m_freq2 == 0) {
    if (m_muxToggle) {
      m_muxToggle = false;
      applyFrequency(m_freq1);
    }
    return;
  }
  if (m_freq1 == 0 && m_freq2 > 0) {
    if (!m_muxToggle) {
      m_muxToggle = true;
      applyFrequency(m_freq2);
    }
    return;
  }

  // Time-multiplex 2 channels every 6 ms
  uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);
  if (nowMs - m_lastMuxToggleMs >= 6) {
    m_lastMuxToggleMs = nowMs;
    m_muxToggle = !m_muxToggle;
    applyFrequency(m_muxToggle ? m_freq1 : m_freq2);
  }
}

void AudioEngine::setVolume(uint8_t volume) {
  m_volume = std::min((uint8_t)20, volume);
  ESP_LOGI(TAG, "Volume set to %d", m_volume);
  if (m_freq1 > 0 || m_freq2 > 0) {
    applyFrequency(m_muxToggle ? m_freq1 : (m_freq1 > 0 ? m_freq1 : m_freq2));
  }
}

void AudioEngine::volumeUp() {
  if (m_volume < 20) {
    setVolume(m_volume + 1);
  }
}

void AudioEngine::volumeDown() {
  if (m_volume > 0) {
    setVolume(m_volume - 1);
  }
}
