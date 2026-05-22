#include "config.h"

#ifndef MATRIX_EXT

// ============================================================
//   display_8x32.cpp : MAX7219 FC16 1 枚 (8×32) 用物理層
// ============================================================

#include "display.h"
#include <MD_MAX72xx.h>
#include <SPI.h>

static MD_MAX72XX mx(MD_MAX72XX::FC16_HW, CS_PIN, MAX_DEVICES);

void display_phys_init() {
    mx.begin();
    mx.control(MD_MAX72XX::SHUTDOWN, MD_MAX72XX::OFF);
    mx.control(MD_MAX72XX::INTENSITY, LED_INTENSITY);
    mx.clear();
}

void display_phys_flush(const bool fb[DISP_ROWS][DISP_COLS]) {
    // 列アドレスを反転して左右ミラーを補正
    for (uint8_t r = 0; r < DISP_ROWS; r++) {
        for (uint8_t c = 0; c < DISP_COLS; c++) {
            mx.setPoint(r, DISP_COLS - 1 - c, fb[r][c]);
        }
    }
}

#endif // !MATRIX_EXT
