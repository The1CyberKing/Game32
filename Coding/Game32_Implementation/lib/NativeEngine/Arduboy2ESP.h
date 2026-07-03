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

// --- Arduino & AVR Compatibility Defines for ESP32 ---
#ifndef PROGMEM
#define PROGMEM
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
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#endif

#ifndef pgm_read_ptr
#define pgm_read_ptr(addr) (*(const void* const *)(addr))
#endif

#ifndef _BV
#define _BV(bit) (1 << (bit))
#endif

typedef uint8_t byte;
typedef uint16_t word;
typedef bool boolean;

inline long random(long maxVal) {
    if (maxVal == 0) return 0;
    return ::random() % maxVal;
}
inline long random(long minVal, long maxVal) {
    if (minVal >= maxVal) return minVal;
    return minVal + (::random() % (maxVal - minVal));
}

class __FlashStringHelper;

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

constexpr int WIDTH = 128;
constexpr int HEIGHT = 64;

// --- Arduboy Colors ---
#define BLACK 0
#define WHITE 1
#define INVERT 2

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
    void on();
    void off();
    void toggle();
    void saveOnOff();
    bool enabled() const;
    void begin();
protected:
    bool audio_enabled{true};
};

// --- Arduboy Tones / Sound ---
class ArduboyTones {
public:
    ArduboyTones(bool (*outEn)() = nullptr);
    void tone(uint16_t freq, uint16_t dur);
    void tone(uint16_t freq1, uint16_t dur1, uint16_t freq2, uint16_t dur2);
    void tone(uint16_t freq1, uint16_t dur1, uint16_t freq2, uint16_t dur2, uint16_t freq3, uint16_t dur3);
    void noTone();
    bool playing();
    void update(); // Must be called periodically
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
private:
    uint8_t m_duration{0};
    uint32_t m_lastTick{0};
};

// --- Arduboy2 Core & Graphics Engine ---
class Arduboy2ESP {
public:
    Arduboy2ESP();
    void begin();
    void beginNoLogo();
    void boot() { begin(); }
    void flashlight() {}
    void systemButtons() {}
    void setRGBled(uint8_t red, uint8_t green, uint8_t blue) {}
    void setRGBled(uint8_t red, uint8_t green, uint8_t blue, uint8_t pwm) {}

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
    void write(uint8_t c);
    void print(const char* str);
    void print(int val);
    void print(long val);
    void print(unsigned long val);
    void println(const char* str = "");
    void println(int val);
    void drawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t bg, uint8_t size);

    // Audio instance
    Arduboy2Audio audio;

    // Standard Arduboy public/protected members for direct access
    uint8_t currentButtonState{0};
    uint8_t previousButtonState{0};
    uint8_t frameCount{0};

private:
    uint8_t m_frameRate{60};
    uint32_t m_lastFrameTimeMs{0};
    uint32_t m_frameDurationMs{16};
    int16_t m_cursorX{0};
    int16_t m_cursorY{0};
    uint8_t m_textColor{WHITE};
    uint8_t m_textBgColor{BLACK};
    uint8_t m_textSize{1};
};

typedef Arduboy2ESP Arduboy2;
typedef Arduboy2ESP Arduboy2Base;

// Arduino Timing Utilities
inline unsigned long millis() {
    return (unsigned long)(esp_timer_get_time() / 1000);
}
inline unsigned long micros() {
    return (unsigned long)(esp_timer_get_time());
}
inline void delay(unsigned long ms) {
    unsigned long start = millis();
    while (millis() - start < ms) {
        vTaskDelay(1);
    }
}
inline void delayMicroseconds(unsigned int us) {
    unsigned long start = micros();
    while (micros() - start < us) {
        // Busy wait
    }
}

#endif // ARDUBOY2_ESP_H
