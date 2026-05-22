#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================
//   display.h : 論理座標 API (向き・マトリクスサイズに依存しない)
// ============================================================

enum Orientation {
    ORIENT_HORIZONTAL = 0,   // 通常横持ち
    ORIENT_VERTICAL   = 1    // 時計回り 90° 回転 (縦持ち)
};

void display_init();
void display_clear();
void display_update();        // バッファを物理マトリクスへ転送

void display_set(int16_t x, int16_t y, bool on);
bool display_get(int16_t x, int16_t y);

void display_set_orientation(Orientation o);

// 3×5 ピクセルフォント (数字 0-9)
void display_draw_digit(int16_t x, int16_t y, uint8_t digit);
// 上記を scale 倍 (各ピクセルを scale×scale ブロック) で描画
void display_draw_digit_scaled(int16_t x, int16_t y, uint8_t digit, uint8_t scale);

// タイマーゲージ (最右列 col 31, ORIENT_HORIZONTAL 前提)
void display_set_timer_gauge(uint8_t filled);

// ---- 物理層 (display_8x32.cpp / display_16x32.cpp で実装) ----
void display_phys_init();
void display_phys_flush(const bool fb[DISP_ROWS][DISP_COLS]);
