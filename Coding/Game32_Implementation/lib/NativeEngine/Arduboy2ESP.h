#ifndef ARDUBOY2_ESP_H
#define ARDUBOY2_ESP_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include "esp_timer.h"
#include "DisplayManager.h"
#include "InputManager.h"
#include "AudioEngine.h"
#include "Sprites.h"
#ifdef ARDUINO
#include <Arduino.h>
#else
#include "Print.h"
#include "AVRCompat.h"
#endif
#include "ArduboyTonesPitches.h"

// --- Arduino & AVR Compatibility Defines for ESP32 ---
#ifndef PROGMEM
#define PROGMEM
#endif

#include <type_traits>
#ifdef ARDUINO
#undef pgm_read_word
#undef pgm_read_word_near

template <typename T>
inline typename std::enable_if<std::is_pointer<T>::value, T>::type
arduboy_pgm_read_word(const T* addr) {
    return (T)pgm_read_ptr(addr);
}

template <typename T>
inline typename std::enable_if<!std::is_pointer<T>::value, uint16_t>::type
arduboy_pgm_read_word(const T* addr) {
    return *(const uint16_t*)addr;
}

template <typename T>
inline typename std::enable_if<std::is_pointer<T>::value, T>::type
arduboy_pgm_read_word(T* addr) {
    return (T)pgm_read_ptr(addr);
}

template <typename T>
inline typename std::enable_if<!std::is_pointer<T>::value, uint16_t>::type
arduboy_pgm_read_word(T* addr) {
    return *(const uint16_t*)addr;
}

inline uint16_t arduboy_pgm_read_word(uint32_t addr) {
    return *(const uint16_t*)addr;
}
inline uint16_t arduboy_pgm_read_word(int addr) {
    return *(const uint16_t*)addr;
}

#define pgm_read_word(addr) arduboy_pgm_read_word(addr)
#define pgm_read_word_near(addr) arduboy_pgm_read_word(addr)
#endif

#ifndef PSTR
#define PSTR(s) (s)
#endif

#ifndef F
#define F(s) (s)
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#endif

#ifndef pgm_read_word
inline uint16_t _pgm_read_word(const void* addr) { return *(const uint16_t*)addr; }
inline const uint8_t* _pgm_read_word(const uint8_t* const* addr) { return *addr; }
inline const uint8_t* _pgm_read_word(uint8_t* const* addr) { return *addr; }
#define pgm_read_word(addr) _pgm_read_word(addr)
#endif

#ifndef pgm_read_ptr
#define pgm_read_ptr(addr) (*(const void* const *)(addr))
#endif

#ifndef _BV
#define _BV(bit) (1 << (bit))
#endif

// byte, word, boolean, random are provided by the real Arduino.h

class __FlashStringHelper;

#ifndef ARDUBOY2_NO_POINT
struct Point {
    int16_t x{0};
    int16_t y{0};
    constexpr Point() = default;
    constexpr Point(int16_t _x, int16_t _y) : x(_x), y(_y) {}
};
struct Rect {
    int16_t x{0}, y{0}, width{0}, height{0};
    constexpr Rect() = default;
    constexpr Rect(int16_t _x, int16_t _y, int16_t _w, int16_t _h) : x(_x), y(_y), width(_w), height(_h) {}
};
#endif

constexpr int WIDTH = 128;
constexpr int HEIGHT = 64;

// --- Arduboy Colors ---
#define BLACK 0
#define WHITE 1
#define INVERT 2

// --- Arduboy LEDs ---
#define RED_LED 10
#define GREEN_LED 11
#define BLUE_LED 9

// --- Arduboy Button Bitmasks ---
#define UP_BUTTON     0x08
#define DOWN_BUTTON   0x04
#define LEFT_BUTTON   0x20
#define RIGHT_BUTTON  0x40
#define A_BUTTON      0x01
#define B_BUTTON      0x02
#define START_BUTTON  0x80
#define SELECT_BUTTON 0x10
// --- AVR Hardware Timer Emulation & Dummies ---
extern volatile uint16_t TCCR3A, TCCR3B, OCR3A;
extern volatile uint16_t TCCR4A, TCCR4B, OCR4A, OCR4C;
#define COM3A0 0
#define WGM32 0
#define CS31 0
#define CS40 0
#define TIMER3_COMPA_vect 0
#define TIMER1_COMPA_vect 1
#ifndef ISR
#define ISR(vect) extern "C" void dummy_isr_##vect()
#endif

// --- Arduboy Audio Control ---
class Arduboy2Audio {
public:
    static void on();
    static void off();
    static void toggle();
    static void saveOnOff();
    static bool enabled();
    static void begin();
protected:
    static bool audio_enabled;
};

typedef Arduboy2Audio ArduboyAudio;


class ArduboyTones {
public:
    ArduboyTones(bool (*outEn)() = nullptr);
    void tone(uint16_t freq, uint16_t dur);
    void tone(uint16_t freq1, uint16_t dur1, uint16_t freq2, uint16_t dur2);
    void tone(uint16_t freq1, uint16_t dur1, uint16_t freq2, uint16_t dur2, uint16_t freq3, uint16_t dur3);
    void tones(const uint16_t *tones);
    void tonesInRAM(uint16_t *tones);
    void noTone();
    bool playing();
    void update();
    void initChannel(uint8_t channel) {}
    void playScore(const uint8_t* score) {}
    void stopScore() {}
private:
    uint16_t m_freq1{0}, m_dur1{0};
    uint16_t m_freq2{0}, m_dur2{0};
    uint16_t m_freq3{0}, m_dur3{0};
    uint32_t m_startMs{0};
    int m_step{0};
};

class BeepPin1 {
public:
    BeepPin1();
    void begin();
    void tone(uint16_t count);
    void tone(uint16_t count, uint8_t dur);
    void timer(); // Called in game loop
    void noTone();
    static uint16_t freq(float hz) { return hz > 0 ? (uint16_t)(1000000.0f / hz) : 0; }
private:
    uint8_t m_duration{0};
    uint32_t m_lastTick{0};
};

class BeepPin2 {
public:
    BeepPin2();
    void begin();
    void tone(uint16_t count);
    void tone(uint16_t count, uint8_t dur);
    void timer(); // Called in game loop
    void noTone();
    static uint16_t freq(float hz) { return hz > 0 ? (uint16_t)(1000000.0f / hz) : 0; }
private:
    uint8_t m_duration{0};
    uint32_t m_lastTick{0};
};

// --- Arduboy2 Core & Graphics Engine ---
class Arduboy2ESP : public Print {
public:
    Arduboy2ESP();
    bool collide(Point point, Rect rect);
    bool collide(Rect rect1, Rect rect2);
    void begin();
    void beginNoLogo();
    void boot() { begin(); }
    void flashlight() {}
    void systemButtons() {}
    void setRGBled(uint8_t red, uint8_t green, uint8_t blue) {}
    void setRGBled(uint8_t red, uint8_t green, uint8_t blue, uint8_t pwm) {}

    static void setGameName(const char* name);
    static const char* getGameName();

    // Display & Frame Pacing
    void clear();
    void display();
    void display(bool clear);
    void syncAudioTimers();
    void setFrameRate(uint8_t rate);
    bool nextFrame();
    bool everyXFrames(uint8_t frames);
    int cpuLoad();
    void initRandomSeed();

    // Screen Dimensions & Buffer Access
    static constexpr int WIDTH = ::WIDTH;
    static constexpr int HEIGHT = ::HEIGHT;
    int width() const { return WIDTH; }
    int height() const { return HEIGHT; }
    uint8_t* getBuffer() { return sBuffer; }
    static uint8_t sBuffer[::WIDTH * ::HEIGHT / 8];

    // Buttons
    void pollButtons();
    void clearButtonState();
    bool pressed(uint8_t buttons);
    bool notPressed(uint8_t buttons);
    bool justPressed(uint8_t buttons);
    bool justReleased(uint8_t buttons);
    uint8_t buttonsState();

    // Graphics Primitives
    void drawPixel(int16_t x, int16_t y, uint8_t color = WHITE);
    uint8_t getPixel(uint8_t x, uint8_t y);
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color = WHITE);
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint8_t color = WHITE);
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint8_t color = WHITE);
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color = WHITE);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color = WHITE);
    void fillScreen(uint8_t color = WHITE) { if (color == BLACK) clear(); else fillRect(0, 0, WIDTH, HEIGHT, color); }
    void sendLCDCommand(uint8_t command) {}
    void delayShort(uint16_t ms) { delay(ms / 3); }
    void drawCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color = WHITE);
    void fillCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color = WHITE);
    void drawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, uint8_t color);
    void fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, int16_t delta, uint8_t color);
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color = WHITE);
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color = WHITE);
    void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color = WHITE);
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color = WHITE);
    void drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, uint8_t color = WHITE);
    void drawSlowXYBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, uint8_t color = WHITE);
    void drawCompressed(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t color = WHITE);

    // Text Output (Print compatibility)
    void setCursor(int16_t x, int16_t y);
    void setTextColor(uint8_t color);
    void setTextSize(uint8_t s);
    void setTextBackground(uint8_t bg);
    uint8_t getTextColor() const { return m_textColor; }
    uint8_t getTextBackground() const { return m_textBgColor; }
    uint8_t getTextSize() const { return m_textSize; }
    int16_t getCursorX() const { return m_cursorX; }
    int16_t getCursorY() const { return m_cursorY; }
    size_t write(uint8_t c) override;
    void drawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t bg, uint8_t size);

    // Audio instance
    Arduboy2Audio audio;
    ArduboyTones tunes;

    // Standard Arduboy public/protected members for direct access
    uint8_t currentButtonState{0};
    uint8_t previousButtonState{0};
    uint8_t frameCount{0};

protected:
    uint8_t m_frameRate{60};
    uint32_t m_lastFrameTimeMs{0};
    uint32_t m_frameDurationMs{16};
public:
    union { int16_t m_cursorX; int16_t cursor_x; };
    union { int16_t m_cursorY; int16_t cursor_y; };
    union { uint8_t m_textColor; uint8_t textColor; };
    union { uint8_t m_textBgColor; uint8_t textBackground; };
    union { uint8_t m_textSize; uint8_t textSize; uint8_t textsize; };
    union { bool m_textWrap; bool textWrap; bool wrap; };
};

typedef Arduboy2ESP Arduboy2;
typedef Arduboy2ESP Arduboy2Base;
typedef Arduboy2ESP Arduboy;

// Timing utilities are provided by real Arduino.h

#ifndef HAS_EEPROM_STUBS
inline void initEEPROM(bool clear) { (void)clear; }
inline int EEPROMReadInt(int address) { (void)address; return 0; }
inline void EEPROMWriteInt(int address, int val) { (void)address; (void)val; }
#endif

#endif // ARDUBOY2_ESP_H
