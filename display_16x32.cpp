#include "config.h"

#ifdef MATRIX_EXT

// ============================================================
//   display_16x32.cpp : MAX7219 FC16 8 個 (16×32) 用物理層
// ============================================================
// 配線前提:
//   FC16×4 のストリップ 2 本を上下に並べ、1 本のチェーンに連結。
//   Arduino → 上ストリップ(device 0-3) → 下ストリップ(device 4-7)。
//   下ストリップは配線の都合で 180° 回転して設置している。
// MD_MAX72XX は 8 個チェーンを 8 行 × 64 列として扱うため:
//   上段 (論理 y 0-7)  → device 0-3 = hwCol 0-31、列ミラー (31-x)
//   下段 (論理 y 8-15) → device 4-7 = hwCol 32-63、180°回転ぶん行・列を反転
// ------------------------------------------------------------

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
    for (uint8_t y = 0; y < DISP_ROWS; y++) {
        for (uint8_t x = 0; x < DISP_COLS; x++) {
            if (y < 8) {
                mx.setPoint(y,     31 - x, fb[y][x]);       // 上段ストリップ
            } else {
                // 下段は 180° 回転設置のため、行・列とも反転して補正
                mx.setPoint(7 - (y - 8), 32 + x, fb[y][x]);
            }
        }
    }
}

#endif // MATRIX_EXT
