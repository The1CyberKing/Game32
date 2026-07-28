#include "Arduboy2ESP.h"
#include "../StateMachine/States/OverlayState.h"
#include "EEPROM.h"
#include "PowerManager.h"
#include "esp_log.h"
#include <algorithm>

#ifndef GAME_NAME
#define GAME_NAME "default"
#endif

[[maybe_unused]] static const char* TAG = "Arduboy2ESP";

static char s_currentGameName[64] = GAME_NAME;
void Arduboy2ESP::setGameName(const char* name) {
    if (name) strncpy(s_currentGameName, name, sizeof(s_currentGameName) - 1);
}
const char* Arduboy2ESP::getGameName() {
    return s_currentGameName;
}

// --- Simple 5x7 ASCII Font Table (characters 32 to 127) ---
static const uint8_t PROGMEM font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // 32 space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33 !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // 34 "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35 #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36 $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // 37 %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // 38 &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // 39 '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40 (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41 )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // 42 *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43 +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // 44 ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // 45 -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // 46 .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // 47 /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 50 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 51 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 53 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 54 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 55 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 56 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 57 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // 58 :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // 59 ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // 60 <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // 61 =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // 62 >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // 63 ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // 64 @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 65 A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 66 B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 67 C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 68 D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 69 E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 70 F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 71 G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 72 H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 73 I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 74 J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 75 K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 76 L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 77 M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 78 N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 79 O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 80 P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 81 Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 82 R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 83 S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 84 T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 85 U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 86 V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 87 W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 88 X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 89 Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 90 Z
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // 91 [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // 92 backslash
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // 93 ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // 94 ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // 95 _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // 96 `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // 97 a
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // 98 b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // 99 c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // 100 d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // 101 e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // 102 f
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // 103 g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // 104 h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // 105 i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // 106 j
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // 107 k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // 108 l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // 109 m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // 110 n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // 111 o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // 112 p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // 113 q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // 114 r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // 115 s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // 116 t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 117 u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 118 v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 119 w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // 120 x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // 121 y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // 122 z
    {0x00, 0x08, 0x36, 0x41, 0x00}, // 123 {
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // 124 |
    {0x00, 0x41, 0x36, 0x08, 0x00}, // 125 }
    {0x10, 0x08, 0x08, 0x10, 0x08}  // 126 ~
};

// --- Arduboy2Audio ---
bool Arduboy2Audio::audio_enabled = true;
void Arduboy2Audio::on() { audio_enabled = true; }
void Arduboy2Audio::off() { audio_enabled = false; }
void Arduboy2Audio::toggle() { audio_enabled = !audio_enabled; }
void Arduboy2Audio::saveOnOff() {}
bool Arduboy2Audio::enabled() { return audio_enabled; }
void Arduboy2Audio::begin() { audio_enabled = true; }

// --- BeepPin1 ---
BeepPin1::BeepPin1() {}
void BeepPin1::begin() {}
void BeepPin1::tone(uint16_t count) { tone(count, 10); }
void BeepPin1::tone(uint16_t count, uint8_t dur) {
    m_duration = dur;
    m_lastTick = millis();
    uint16_t freq = 1000000 / (count * 2 + 1);
    AudioEngine::getInstance().playTone1(freq);
}
void BeepPin1::timer() {
    if (m_duration > 0) {
        if (millis() - m_lastTick >= 16) {
            m_lastTick = millis();
            m_duration--;
            if (m_duration == 0) {
                noTone();
            }
        }
    }
}
void BeepPin1::noTone() {
    m_duration = 0;
    AudioEngine::getInstance().playTone1(0);
}

// --- BeepPin2 ---
BeepPin2::BeepPin2() {}
void BeepPin2::begin() {}
void BeepPin2::tone(uint16_t count) { tone(count, 10); }
void BeepPin2::tone(uint16_t count, uint8_t dur) {
    m_duration = dur;
    m_lastTick = millis();
    uint16_t freq = 1000000 / (count * 2 + 1);
    AudioEngine::getInstance().playTone2(freq);
}
void BeepPin2::timer() {
    if (m_duration > 0) {
        if (millis() - m_lastTick >= 16) {
            m_lastTick = millis();
            m_duration--;
            if (m_duration == 0) {
                noTone();
            }
        }
    }
}
void BeepPin2::noTone() {
    m_duration = 0;
    AudioEngine::getInstance().playTone2(0);
}

volatile uint16_t TCCR3A = 0, TCCR3B = 0, OCR3A = 0;
volatile uint16_t TCCR4A = 0, TCCR4B = 0, OCR4A = 0, OCR4C = 0;

extern "C" __attribute__((weak)) void dummy_isr_0() {}
extern "C" __attribute__((weak)) void dummy_isr_1() {}

void Arduboy2ESP::syncAudioTimers() {
    static bool timer3Active = false;
    static bool timer4Active = false;

    // Run emulated AVR Timer ISRs if timer is configured
    if (TCCR3B != 0 || TCCR4B != 0 || TCCR3A != 0 || TCCR4A != 0) {
        dummy_isr_0();
        dummy_isr_1();
    }

    // Timer 3 -> Channel 1
    if (TCCR3A != 0 && OCR3A > 0) {
        uint16_t f1 = 2000000 / (2 * (OCR3A + 1));
        AudioEngine::getInstance().playTone1(f1);
        timer3Active = true;
    } else if (timer3Active) {
        AudioEngine::getInstance().playTone1(0);
        timer3Active = false;
    }

    // Timer 4 -> Channel 2
    if ((TCCR4A != 0 || TCCR4B != 0) && OCR4A > 0) {
        uint16_t f2 = 62500 / (OCR4A + 1);
        if (f2 < 100) f2 = 100 + (OCR4A * 5); // map bytebeat values to audible frequency range
        AudioEngine::getInstance().playTone2(f2);
        timer4Active = true;
    } else if (timer4Active) {
        AudioEngine::getInstance().playTone2(0);
        timer4Active = false;
    }
}

// --- Arduboy2ESP ---
uint8_t Arduboy2ESP::sBuffer[::WIDTH * ::HEIGHT / 8];

bool Arduboy2ESP::collide(Point point, Rect rect) {
    return ((point.x >= rect.x) && (point.x < rect.x + rect.width) &&
            (point.y >= rect.y) && (point.y < rect.y + rect.height));
}

bool Arduboy2ESP::collide(Rect rect1, Rect rect2) {
    return !(rect2.x                >= rect1.x + rect1.width  ||
             rect2.x + rect2.width  <= rect1.x                ||
             rect2.y                >= rect1.y + rect1.height ||
             rect2.y + rect2.height <= rect1.y);
}

Arduboy2ESP::Arduboy2ESP() {
    clear();
}

void Arduboy2ESP::begin() {
    DisplayManager::getInstance().initialize();
    InputManager::getInstance().initialize();
    AudioEngine::getInstance().init();
    audio.begin();
    clear();
    currentButtonState = 0;
    previousButtonState = 0;
    
    // Auto-Load the Save File
    EEPROM.loadFromFile(getGameName());
}

void Arduboy2ESP::beginNoLogo() {
    begin();
}

void Arduboy2ESP::clear() {
    memset(sBuffer, 0, sizeof(sBuffer));
}

void Arduboy2ESP::display() {
    syncAudioTimers();
    DisplayManager::getInstance().drawArduboyFrame(sBuffer);
    
    if (OverlayState::getInstance().isActive()) {
        OverlayState::getInstance().onDraw();
    }
    
    // ALWAYS composite and push at the very end
    DisplayManager::getInstance().renderPipelinePush();
    AudioEngine::getInstance().update();
}

void Arduboy2ESP::display(bool clearBuf) {
    display();
    if (clearBuf) clear();
}

void Arduboy2ESP::setFrameRate(uint8_t rate) {
    m_frameRate = (rate > 0) ? rate : 60;
    m_frameDurationMs = 1000 / m_frameRate;
}

bool Arduboy2ESP::nextFrame() {
    PowerManager::getInstance().executeAdaptiveKeepalive();
    
    if (OverlayState::getInstance().isActive()) {
        OverlayState::getInstance().onUpdate();
        display(); // Force display update while game logic is stalled
        return false;
    }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - m_lastFrameTimeMs < m_frameDurationMs) {
        return false;
    }
    m_lastFrameTimeMs = now;
    frameCount++;
    return true;
}

bool Arduboy2ESP::everyXFrames(uint8_t frames) {
    return (frameCount % frames) == 0;
}

int Arduboy2ESP::cpuLoad() { return 10; } // Very low load natively!

void Arduboy2ESP::initRandomSeed() {
    randomSeed(esp_timer_get_time());
}

ArduboyTones::ArduboyTones(bool (*outEn)()) {}
void ArduboyTones::tone(uint16_t freq, uint16_t dur) {
    m_freq1 = freq; m_dur1 = dur;
    m_freq2 = 0; m_dur2 = 0;
    m_startMs = millis();
    AudioEngine::getInstance().playTone(freq, 0);
}
void ArduboyTones::tone(uint16_t freq1, uint16_t dur1, uint16_t freq2, uint16_t dur2) {
    m_freq1 = freq1; m_dur1 = dur1;
    m_freq2 = freq2; m_dur2 = dur2;
    m_startMs = millis();
    AudioEngine::getInstance().playTone(freq1, freq2);
}
void ArduboyTones::tone(uint16_t freq1, uint16_t dur1, uint16_t freq2, uint16_t dur2, uint16_t freq3, uint16_t dur3) {
    tone(freq1, dur1, freq2, dur2);
}
void ArduboyTones::tones(const uint16_t *tones) {}
void ArduboyTones::tonesInRAM(uint16_t *tones) {}
void ArduboyTones::noTone() {
    m_freq1 = 0; m_freq2 = 0;
    AudioEngine::getInstance().stopTone();
}
bool ArduboyTones::playing() { return m_freq1 > 0 || m_freq2 > 0; }
void ArduboyTones::update() {
    if (m_freq1 > 0 && millis() - m_startMs >= m_dur1) {
        if (m_freq2 > 0) {
            m_freq1 = m_freq2; m_dur1 = m_dur2;
            m_freq2 = 0; m_dur2 = 0;
            m_startMs = millis();
            AudioEngine::getInstance().playTone(m_freq1, 0);
        } else {
            noTone();
        }
    }
    AudioEngine::getInstance().update();
}

void Arduboy2ESP::clearButtonState() {
    currentButtonState = 0;
    previousButtonState = 0;
}

void Arduboy2ESP::pollButtons() {
    auto& im = InputManager::getInstance();
    if (im.isHeld(6) && im.isHeld(7)) {
        clearButtonState();
        OverlayState::getInstance().onEnter();
        return;
    }

    previousButtonState = currentButtonState;
    currentButtonState = 0;
    if (im.isHeld(0)) currentButtonState |= UP_BUTTON;
    if (im.isHeld(1)) currentButtonState |= DOWN_BUTTON;
    if (im.isHeld(2)) currentButtonState |= LEFT_BUTTON;
    if (im.isHeld(3)) currentButtonState |= RIGHT_BUTTON;
    if (im.isHeld(4)) currentButtonState |= A_BUTTON;
    if (im.isHeld(5)) currentButtonState |= B_BUTTON;
    if (im.isHeld(6)) currentButtonState |= START_BUTTON;
    if (im.isHeld(7)) currentButtonState |= SELECT_BUTTON;
}

bool Arduboy2ESP::pressed(uint8_t buttons) {
    return (currentButtonState & buttons) == buttons;
}

bool Arduboy2ESP::notPressed(uint8_t buttons) {
    return (currentButtonState & buttons) == 0;
}

bool Arduboy2ESP::justPressed(uint8_t buttons) {
    return ((currentButtonState & buttons) == buttons) && ((previousButtonState & buttons) == 0);
}

bool Arduboy2ESP::justReleased(uint8_t buttons) {
    return ((previousButtonState & buttons) == buttons) && ((currentButtonState & buttons) == 0);
}

uint8_t Arduboy2ESP::buttonsState() {
    return currentButtonState;
}

void Arduboy2ESP::drawPixel(int16_t x, int16_t y, uint8_t color) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
    int row = y / 8;
    uint8_t bit = _BV(y & 7);
    if (color == WHITE) {
        sBuffer[(row * WIDTH) + x] |= bit;
    } else if (color == BLACK) {
        sBuffer[(row * WIDTH) + x] &= ~bit;
    } else if (color == INVERT) {
        sBuffer[(row * WIDTH) + x] ^= bit;
    }
}

uint8_t Arduboy2ESP::getPixel(uint8_t x, uint8_t y) {
    if (x >= WIDTH || y >= HEIGHT) return 0;
    return (sBuffer[(y / 8) * WIDTH + x] & _BV(y & 7)) ? 1 : 0;
}

void Arduboy2ESP::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color) {
    int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy, e2;

    while (true) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Arduboy2ESP::drawFastVLine(int16_t x, int16_t y, int16_t h, uint8_t color) {
    for (int16_t i = 0; i < h; i++) drawPixel(x, y + i, color);
}

void Arduboy2ESP::drawFastHLine(int16_t x, int16_t y, int16_t w, uint8_t color) {
    for (int16_t i = 0; i < w; i++) drawPixel(x + i, y, color);
}

void Arduboy2ESP::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
}

void Arduboy2ESP::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    for (int16_t i = 0; i < w; i++) {
        drawFastVLine(x + i, y, h, color);
    }
}

void Arduboy2ESP::drawCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color) {
    int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
    drawPixel(x0, y0 + r, color);
    drawPixel(x0, y0 - r, color);
    drawPixel(x0 + r, y0, color);
    drawPixel(x0 - r, y0, color);
    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        drawPixel(x0 + x, y0 + y, color);
        drawPixel(x0 - x, y0 + y, color);
        drawPixel(x0 + x, y0 - y, color);
        drawPixel(x0 - x, y0 - y, color);
        drawPixel(x0 + y, y0 + x, color);
        drawPixel(x0 - y, y0 + x, color);
        drawPixel(x0 + y, y0 - x, color);
        drawPixel(x0 - y, y0 - x, color);
    }
}

void Arduboy2ESP::fillCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color) {
    drawFastVLine(x0, y0 - r, 2 * r + 1, color);
    fillCircleHelper(x0, y0, r, 3, 0, color);
}

void Arduboy2ESP::drawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, uint8_t color) {
    int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        if (cornername & 0x4) {
            drawPixel(x0 + x, y0 + y, color);
            drawPixel(x0 + y, y0 + x, color);
        }
        if (cornername & 0x2) {
            drawPixel(x0 + x, y0 - y, color);
            drawPixel(x0 + y, y0 - x, color);
        }
        if (cornername & 0x8) {
            drawPixel(x0 - y, y0 + x, color);
            drawPixel(x0 - x, y0 + y, color);
        }
        if (cornername & 0x1) {
            drawPixel(x0 - y, y0 - x, color);
            drawPixel(x0 - x, y0 - y, color);
        }
    }
}

void Arduboy2ESP::fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, int16_t delta, uint8_t color) {
    int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
    while (x < y) {
        if (f >= 0) {
            drawFastVLine(x0 + x, y0 - y, 2 * y + 1 + delta, color);
            drawFastVLine(x0 - x, y0 - y, 2 * y + 1 + delta, color);
            y--; ddF_y += 2; f += ddF_y;
        }
        x++; ddF_x += 2; f += ddF_x;
        if (cornername & 0x1) {
            drawFastVLine(x0 + y, y0 - x, 2 * x + 1 + delta, color);
            drawFastVLine(x0 - y, y0 - x, 2 * x + 1 + delta, color);
        }
    }
}

void Arduboy2ESP::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color) {
    drawFastHLine(x + r, y, w - 2 * r, color);
    drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
    drawFastVLine(x, y + r, h - 2 * r, color);
    drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
    drawCircleHelper(x + r, y + r, r, 1, color);
    drawCircleHelper(x + w - r - 1, y + r, r, 2, color);
    drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, color);
    drawCircleHelper(x + r, y + h - r - 1, r, 8, color);
}

void Arduboy2ESP::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color) {
    fillRect(x + r, y, w - 2 * r, h, color);
    fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color);
    fillCircleHelper(x + r, y + r, r, 2, h - 2 * r - 1, color);
}

void Arduboy2ESP::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color) {
    drawLine(x0, y0, x1, y1, color);
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x0, y0, color);
}

void Arduboy2ESP::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color) {
    int16_t a, b, y, last;
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
    if (y1 > y2) { std::swap(y2, y1); std::swap(x2, x1); }
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }

    if (y0 == y2) {
        a = b = x0;
        if (x1 < a) a = x1;
        else if (x1 > b) b = x1;
        if (x2 < a) a = x2;
        else if (x2 > b) b = x2;
        drawFastHLine(a, y0, b - a + 1, color);
        return;
    }

    int16_t dx01 = x1 - x0, dy01 = y1 - y0;
    int16_t dx02 = x2 - x0, dy02 = y2 - y0;
    int16_t dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t sa = 0, sb = 0;
    if (y1 == y2) last = y1; else last = y1 - 1;

    for (y = y0; y <= last; y++) {
        a = x0 + sa / dy01;
        b = x0 + sb / dy02;
        sa += dx01;
        sb += dx02;
        if (a > b) std::swap(a, b);
        drawFastHLine(a, y, b - a + 1, color);
    }

    sa = dx12 * (y - y1);
    sb = dx02 * (y - y0);
    for (; y <= y2; y++) {
        a = x1 + sa / dy12;
        b = x0 + sb / dy02;
        sa += dx12;
        sb += dx02;
        if (a > b) std::swap(a, b);
        drawFastHLine(a, y, b - a + 1, color);
    }
}

void Arduboy2ESP::drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, uint8_t color) {
    if (!bitmap) return;
    int16_t byteWidth = (h + 7) / 8;
    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            if (pgm_read_byte(bitmap + i * byteWidth + j / 8) & _BV(j & 7)) {
                drawPixel(x + i, y + j, color);
            }
        }
    }
}

void Arduboy2ESP::drawSlowXYBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, uint8_t color) {
    drawBitmap(x, y, bitmap, w, h, color);
}

void Arduboy2ESP::drawCompressed(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t color) {
    if (!bitmap) return;
    // Basic RLE decoder for Arduboy compressed sprites
    int16_t w = pgm_read_word(bitmap);
    int16_t h = pgm_read_word(bitmap + 2);
    bitmap += 4;
    int16_t col = 0, row = 0;
    while (col < w && row < h) {
        uint8_t count = pgm_read_byte(bitmap++);
        uint8_t data = pgm_read_byte(bitmap++);
        for (int i = 0; i < count; i++) {
            for (int bit = 0; bit < 8; bit++) {
                if (data & (1 << bit)) {
                    drawPixel(x + col, y + row + bit, color);
                }
            }
            col++;
            if (col >= w) {
                col = 0;
                row += 8;
            }
        }
    }
}

void Arduboy2ESP::setCursor(int16_t x, int16_t y) {
    m_cursorX = x;
    m_cursorY = y;
}

void Arduboy2ESP::setTextColor(uint8_t color) {
    m_textColor = color;
}

void Arduboy2ESP::setTextSize(uint8_t s) {
    m_textSize = (s > 0) ? s : 1;
}

void Arduboy2ESP::setTextBackground(uint8_t bg) {
    m_textBgColor = bg;
}

void Arduboy2ESP::drawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t bg, uint8_t size) {
    if (c < 32 || c > 126) c = 32;
    const uint8_t* chr = font5x7[c - 32];
    for (int8_t i = 0; i < 5; i++) {
        uint8_t line = pgm_read_byte(&chr[i]);
        for (int8_t j = 0; j < 8; j++) {
            if (line & 0x01) {
                if (size == 1) drawPixel(x + i, y + j, color);
                else fillRect(x + i * size, y + j * size, size, size, color);
            } else if (bg != color) {
                if (size == 1) drawPixel(x + i, y + j, bg);
                else fillRect(x + i * size, y + j * size, size, size, bg);
            }
            line >>= 1;
        }
    }
}

size_t Arduboy2ESP::write(uint8_t c) {
    if (c == '\n') {
        m_cursorY += m_textSize * 8;
        m_cursorX = 0;
    } else if (c == '\r') {
        m_cursorX = 0;
    } else {
        drawChar(m_cursorX, m_cursorY, c, m_textColor, m_textBgColor, m_textSize);
        m_cursorX += m_textSize * 6;
        if (m_cursorX > WIDTH - (m_textSize * 6)) {
            write('\n');
        }
    }
    return 1;
}

#include "../../include/types.h"
__attribute__((weak)) SystemContext sysContext;
__attribute__((weak)) SemaphoreHandle_t g_i2cMutex = NULL;
