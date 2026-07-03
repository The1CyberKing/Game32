#include "Sprites.h"

void Sprites::drawOverwrite(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame) {
    if (!bitmap) return;
    uint8_t w = pgm_read_byte(&bitmap[0]);
    uint8_t h = pgm_read_byte(&bitmap[1]);
    int16_t pages = (h + 7) / 8;
    const uint8_t* frameData = bitmap + 2 + frame * (w * pages);
    for (int16_t row = 0; row < h; row++) {
        int16_t py = y + row;
        if (py < 0 || py >= ::HEIGHT) continue;
        int16_t sp = row / 8;
        int16_t sb = row % 8;
        for (int16_t col = 0; col < w; col++) {
            int16_t px = x + col;
            if (px < 0 || px >= ::WIDTH) continue;
            uint8_t byteVal = pgm_read_byte(frameData + sp * w + col);
            int16_t bufIdx = (py / 8) * ::WIDTH + px;
            uint8_t bitMask = 1 << (py % 8);
            if (byteVal & (1 << sb)) {
                Arduboy2ESP::sBuffer[bufIdx] |= bitMask;
            } else {
                Arduboy2ESP::sBuffer[bufIdx] &= ~bitMask;
            }
        }
    }
}

void Sprites::drawSelfMasked(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame) {
    if (!bitmap) return;
    uint8_t w = pgm_read_byte(&bitmap[0]);
    uint8_t h = pgm_read_byte(&bitmap[1]);
    int16_t pages = (h + 7) / 8;
    const uint8_t* frameData = bitmap + 2 + frame * (w * pages);
    for (int16_t row = 0; row < h; row++) {
        int16_t py = y + row;
        if (py < 0 || py >= ::HEIGHT) continue;
        int16_t sp = row / 8;
        int16_t sb = row % 8;
        for (int16_t col = 0; col < w; col++) {
            int16_t px = x + col;
            if (px < 0 || px >= ::WIDTH) continue;
            uint8_t byteVal = pgm_read_byte(frameData + sp * w + col);
            if (byteVal & (1 << sb)) {
                Arduboy2ESP::sBuffer[(py / 8) * ::WIDTH + px] |= (1 << (py % 8));
            }
        }
    }
}

void Sprites::drawExternalMask(int16_t x, int16_t y, const uint8_t *bitmap, const uint8_t *mask, uint8_t frame, uint8_t mask_frame) {
    if (!bitmap || !mask) return;
    uint8_t w = pgm_read_byte(&bitmap[0]);
    uint8_t h = pgm_read_byte(&bitmap[1]);
    int16_t pages = (h + 7) / 8;
    const uint8_t* frameData = bitmap + 2 + frame * (w * pages);
    const uint8_t* maskData = mask + 2 + mask_frame * (w * pages);
    for (int16_t row = 0; row < h; row++) {
        int16_t py = y + row;
        if (py < 0 || py >= ::HEIGHT) continue;
        int16_t sp = row / 8;
        int16_t sb = row % 8;
        for (int16_t col = 0; col < w; col++) {
            int16_t px = x + col;
            if (px < 0 || px >= ::WIDTH) continue;
            uint8_t maskByte = pgm_read_byte(maskData + sp * w + col);
            if (maskByte & (1 << sb)) {
                uint8_t byteVal = pgm_read_byte(frameData + sp * w + col);
                int16_t bufIdx = (py / 8) * ::WIDTH + px;
                uint8_t bitMask = 1 << (py % 8);
                if (byteVal & (1 << sb)) {
                    Arduboy2ESP::sBuffer[bufIdx] |= bitMask;
                } else {
                    Arduboy2ESP::sBuffer[bufIdx] &= ~bitMask;
                }
            }
        }
    }
}

void Sprites::drawPlusMask(int16_t x, int16_t y, const uint8_t *bitmap_plus_mask, uint8_t frame) {
    drawSelfMasked(x, y, bitmap_plus_mask, frame);
}

void Sprites::drawErase(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame) {
    if (!bitmap) return;
    uint8_t w = pgm_read_byte(&bitmap[0]);
    uint8_t h = pgm_read_byte(&bitmap[1]);
    int16_t pages = (h + 7) / 8;
    const uint8_t* frameData = bitmap + 2 + frame * (w * pages);
    for (int16_t row = 0; row < h; row++) {
        int16_t py = y + row;
        if (py < 0 || py >= ::HEIGHT) continue;
        int16_t sp = row / 8;
        int16_t sb = row % 8;
        for (int16_t col = 0; col < w; col++) {
            int16_t px = x + col;
            if (px < 0 || px >= ::WIDTH) continue;
            uint8_t byteVal = pgm_read_byte(frameData + sp * w + col);
            if (byteVal & (1 << sb)) {
                Arduboy2ESP::sBuffer[(py / 8) * ::WIDTH + px] &= ~(1 << (py % 8));
            }
        }
    }
}

void Sprites::drawAnimate(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame) {
    drawOverwrite(x, y, bitmap, frame);
}
