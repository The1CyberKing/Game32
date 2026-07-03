#ifndef SPRITES_H
#define SPRITES_H

#include "Arduboy2ESP.h"

class Sprites {
public:
    static void drawOverwrite(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame);
    static void drawSelfMasked(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame);
    static void drawExternalMask(int16_t x, int16_t y, const uint8_t *bitmap, const uint8_t *mask, uint8_t frame, uint8_t mask_frame);
    static void drawPlusMask(int16_t x, int16_t y, const uint8_t *bitmap_plus_mask, uint8_t frame);
    static void drawErase(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame);
    static void drawAnimate(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame);
};

typedef Sprites SpritesB;

#endif // SPRITES_H
