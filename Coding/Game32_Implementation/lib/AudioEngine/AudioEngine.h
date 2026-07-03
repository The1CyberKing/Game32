#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <cstdint>
#include "driver/ledc.h"

class AudioEngine {
public:
  static AudioEngine& getInstance();

  void init();
  
  // Set frequency for Channel 1 and Channel 2 (in Hz). 0 = off.
  void playTone(uint16_t freq1, uint16_t freq2 = 0);
  void playTone1(uint16_t freq);
  void playTone2(uint16_t freq);
  void stopTone();
  
  // Call in game loop or timer tick to handle 2-channel time-multiplexing
  void update();

  // Volume control (0 to 20)
  void setVolume(uint8_t volume);
  uint8_t getVolume() const { return m_volume; }
  void volumeUp();
  void volumeDown();

private:
  AudioEngine() = default;
  ~AudioEngine() = default;
  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  uint32_t getDutyForVolume();
  void applyFrequency(uint16_t freq);

  uint16_t m_freq1{0};
  uint16_t m_freq2{0};
  uint8_t m_volume{12}; // Default medium volume (0=mute, 20=max)
  bool m_muxToggle{false};
  bool m_initialized{false};
  uint32_t m_lastMuxToggleMs{0};
};

#endif // AUDIO_ENGINE_H
