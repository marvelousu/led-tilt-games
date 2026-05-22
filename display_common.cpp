#include "display.h"

// ============================================================
//   display_common.cpp : フレームバッファ・フォント・向き制御
// ============================================================

// fb[row][col] — 物理配置で保持 (col=0 が物理左端、row=0 が物理上端)
static bool fb[DISP_ROWS][DISP_COLS];
static Orientation orient = ORIENT_HORIZONTAL;

// ---- 3×5 数字フォント (0-9) ----
// 各エントリ = 3 列分のバイト (bit0=row0 上端, bit4=row4 下端)
static const uint8_t FONT3x5[10][3] PROGMEM = {
    {0b11111, 0b10001, 0b11111}, // 0
    {0b00000, 0b11111, 0b00000}, // 1
    {0b11101, 0b10101, 0b10111}, // 2
    {0b10101, 0b10101, 0b11111}, // 3
    {0b00111, 0b00100, 0b11111}, // 4
    {0b10111, 0b10101, 0b11101}, // 5
    {0b11111, 0b10101, 0b11101}, // 6
    {0b00001, 0b00001, 0b11111}, // 7
    {0b11111, 0b10101, 0b11111}, // 8
    {0b10111, 0b10101, 0b11111}, // 9
};

// 論理 (x, y) → 物理 fb index (r, c)
//   HORIZONTAL: 論理幅=DISP_COLS, 高さ=DISP_ROWS  — (x, y) = (col, row)
//   VERTICAL:   時計回り 90°回転、論理幅=DISP_ROWS, 高さ=DISP_COLS
//               物理 row = x,  物理 col = (DISP_COLS-1) - y
static inline bool logical_to_phys(int16_t x, int16_t y, uint8_t& r, uint8_t& c) {
    if (orient == ORIENT_HORIZONTAL) {
        if (x < 0 || x >= DISP_COLS || y < 0 || y >= DISP_ROWS) return false;
        c = (uint8_t)x;
        r = (uint8_t)y;
    } else {
        if (x < 0 || x >= DISP_ROWS || y < 0 || y >= DISP_COLS) return false;
        r = (uint8_t)x;
        c = (uint8_t)(DISP_COLS - 1 - y);
    }
    return true;
}

// ---- 公開 API ----
void display_init() {
    memset(fb, 0, sizeof(fb));
    orient = ORIENT_HORIZONTAL;
    display_phys_init();
}

void display_clear() {
    memset(fb, 0, sizeof(fb));
}

void display_update() {
    display_phys_flush(fb);
}

void display_set(int16_t x, int16_t y, bool on) {
    uint8_t r, c;
    if (!logical_to_phys(x, y, r, c)) return;
    fb[r][c] = on;
}

bool display_get(int16_t x, int16_t y) {
    uint8_t r, c;
    if (!logical_to_phys(x, y, r, c)) return false;
    return fb[r][c];
}

void display_set_orientation(Orientation o) {
    orient = o;
}

void display_draw_digit(int16_t x, int16_t y, uint8_t digit) {
    if (digit > 9) return;
    for (uint8_t ci = 0; ci < 3; ci++) {
        uint8_t coldata = pgm_read_byte(&FONT3x5[digit][ci]);
        for (uint8_t ri = 0; ri < 5; ri++) {
            display_set(x + ci, y + ri, (coldata >> ri) & 1);
        }
    }
}

void display_draw_digit_scaled(int16_t x, int16_t y, uint8_t digit, uint8_t scale) {
    if (digit > 9) return;
    for (uint8_t ci = 0; ci < 3; ci++) {
        uint8_t coldata = pgm_read_byte(&FONT3x5[digit][ci]);
        for (uint8_t ri = 0; ri < 5; ri++) {
            if (!((coldata >> ri) & 1)) continue;
            for (uint8_t sx = 0; sx < scale; sx++) {
                for (uint8_t sy = 0; sy < scale; sy++) {
                    display_set(x + ci * scale + sx, y + ri * scale + sy, true);
                }
            }
        }
    }
}

void display_set_timer_gauge(uint8_t filled) {
    if (filled > DISP_ROWS) filled = DISP_ROWS;
    for (uint8_t r = 0; r < DISP_ROWS; r++) {
        fb[r][DISP_COLS - 1] = (r < filled);
    }
}
