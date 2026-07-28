#ifndef AVR_COMPAT_H
#define AVR_COMPAT_H

#include <Arduino.h>
#include <EEPROM.h>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include "esp_timer.h"

#ifndef ARDUINO
inline unsigned long millis() { return (unsigned long)(esp_timer_get_time() / 1000ULL); }
inline void randomSeed(unsigned long seed) { srand((unsigned int)seed); }
inline long random(long howbig) { return howbig == 0 ? 0 : std::rand() % howbig; }
inline long random(long howsmall, long howbig) { if (howsmall >= howbig) return howsmall; return howsmall + std::rand() % (howbig - howsmall); }
#endif

#ifndef pgm_read_ptr
#define pgm_read_ptr(addr) (*(const void **)(addr))
#endif
#ifndef pgm_read_ptr_near
#define pgm_read_ptr_near(addr) pgm_read_ptr(addr)
#endif
#ifndef pgm_read_ptr_far
#define pgm_read_ptr_far(addr) pgm_read_ptr(addr)
#endif

// AVR EEPROM Compatibility Stubs
inline uint8_t eeprom_read_byte(const uint8_t* addr) { return EEPROM.read((size_t)addr); }
inline void eeprom_write_byte(uint8_t* addr, uint8_t val) { EEPROM.write((size_t)addr, val); EEPROM.commit(); }
inline uint16_t eeprom_read_word(const uint16_t* addr) { uint16_t val; EEPROM.get((size_t)addr, val); return val; }
inline void eeprom_write_word(uint16_t* addr, uint16_t val) { EEPROM.put((size_t)addr, val); EEPROM.commit(); }
inline uint32_t eeprom_read_dword(const uint32_t* addr) { uint32_t val; EEPROM.get((size_t)addr, val); return val; }
inline void eeprom_write_dword(uint32_t* addr, uint32_t val) { EEPROM.put((size_t)addr, val); EEPROM.commit(); }
inline void eeprom_read_block(void* dest, const void* src, size_t n) { uint8_t* d = (uint8_t*)dest; size_t s = (size_t)src; for(size_t i=0; i<n; i++) d[i] = EEPROM.read(s+i); }
inline void eeprom_write_block(const void* src, void* dest, size_t n) { const uint8_t* s = (const uint8_t*)src; size_t d = (size_t)dest; for(size_t i=0; i<n; i++) EEPROM.write(d+i, s[i]); EEPROM.commit(); }
inline void eeprom_busy_wait() {}

// AVR Power Management & LCD Stubs
#define power_timer3_enable() ((void)0)
#define power_timer3_disable() ((void)0)
#define power_timer0_disable() ((void)0)
#define OLED_ALL_PIXELS_ON 0xA5
#define PIN_SPEAKER_1 0
#define PIN_SPEAKER_2 1
#define EEPROM_STORAGE_SPACE_START 16
#define EEPROM_AUDIO_ON_OFF 14
#define idle() ((void)0)

// AVR String & Utility Stubs
inline size_t strnlen_P(const char* s, size_t maxlen) { return strnlen(s, maxlen); }
inline size_t strlen_P(const char* s) { return strlen(s); }
#define memcpy_P(dest, src, num) memcpy(dest, src, num)
typedef uint32_t uint_farptr_t;
#define strlen_PF(x) strlen_P((const char*)(x))

#endif // AVR_COMPAT_H
