#include "boot.h"
#include "display.h"
#include "buzzer.h"
#include "config.h"

// ============================================================
//   boot.cpp : 起動アニメ (4 フェーズ, 3000ms)
// ============================================================
// 8 行 / 16 行 共通ソース。16 行版は文字を縦 2 倍に拡大し、
// 粒子数・スイープ幅・放射バーストを増強する (DISP_ROWS 駆動)。
// ------------------------------------------------------------

// ---- フェーズ境界 (ms) ----
#define PH1_END   500
#define PH2_END  1500
#define PH3_END  2500
#define PH4_END  3000
#define PH3_MID  2000   // チャイムを鳴らすタイミング

// ---- 表示スケール (16 行版で増強) ----
#define BVS        ((DISP_ROWS >= 16) ? 2 : 1)               // 縦拡大率
#define GLYPH_H    (5 * BVS)                                 // 文字高さ (5 / 10)
#define TEXT_ROW   ((DISP_ROWS >= 16) ? (DISP_ROWS - GLYPH_H) / 2 : 2)
#define BURST_MAX  (1 + 3 * BVS)                             // 放射半径の最大 (4 / 7)
#define MAX_PARTICLES 16
#define NUM_PARTICLES (8 * BVS)                              // 収束粒子数 (8 / 16)

// ---- 3×5 大文字フォント (A / D / E / G / L / M / S, "LED GAMES" 用) ----
// 各文字 = 3 バイト (col data), bit0=row0 上端, bit4=row4 下端
static const uint8_t FONT3x5_EN[7][3] PROGMEM = {
    {0x1E, 0x05, 0x1E}, // A
    {0x1F, 0x11, 0x0E}, // D
    {0x1F, 0x15, 0x11}, // E
    {0x1F, 0x11, 0x1D}, // G
    {0x1F, 0x10, 0x10}, // L
    {0x1F, 0x02, 0x1F}, // M
    {0x17, 0x15, 0x1D}, // S
};

// "LED GAMES" を 32 列ピッタリに配置: L(0) E(4) D(8) | G(13) A(17) M(21) E(25) S(29)
// 文字間は 1 列ギャップ、LED と GAMES の間は 2 列ギャップ
static const uint8_t LETTER_COL[8] = {0, 4, 8, 13, 17, 21, 25, 29};
static const char    TEXT[8]       = {'L', 'E', 'D', 'G', 'A', 'M', 'E', 'S'};

static int8_t letter_index(char ch) {
    switch (ch) {
        case 'A': return 0;
        case 'D': return 1;
        case 'E': return 2;
        case 'G': return 3;
        case 'L': return 4;
        case 'M': return 5;
        case 'S': return 6;
        default:  return -1;
    }
}

// 文字描画 (16 行版は各フォント行を BVS 行ぶんに引き伸ばす)
static void draw_text() {
    for (uint8_t li = 0; li < 8; li++) {
        int8_t idx = letter_index(TEXT[li]);
        if (idx < 0) continue;
        for (uint8_t ci = 0; ci < 3; ci++) {
            uint8_t coldata = pgm_read_byte(&FONT3x5_EN[idx][ci]);
            for (uint8_t ri = 0; ri < 5; ri++) {
                if (!((coldata >> ri) & 1)) continue;
                for (uint8_t s = 0; s < BVS; s++) {
                    display_set(LETTER_COL[li] + ci, TEXT_ROW + ri * BVS + s, true);
                }
            }
        }
    }
}

// ---- Phase 1: 粒子収束 ----
struct Particle { int8_t sx, sy, tx, ty; };
static Particle particles[MAX_PARTICLES];

static void init_phase1() {
    for (uint8_t i = 0; i < NUM_PARTICLES; i++) {
        particles[i].sx = random(DISP_COLS);
        particles[i].sy = random(DISP_ROWS);
        particles[i].tx = random(DISP_COLS);          // col 0-31 (LED GAMES は全列を使う)
        particles[i].ty = TEXT_ROW + random(GLYPH_H);
    }
}

// ---- Phase 4: ランダム消灯 ----
//   アドレスは (row << 5) | col。16 行版は row が 4bit 必要なため uint16_t。
static uint16_t pixel_list[DISP_ROWS * DISP_COLS];
static uint16_t pixel_count;
static bool     phase4_prepared;

static void init_phase4() {
    pixel_count = 0;
    for (uint8_t r = 0; r < DISP_ROWS; r++) {
        for (uint8_t c = 0; c < DISP_COLS; c++) {
            if (display_get(c, r)) {
                pixel_list[pixel_count++] = ((uint16_t)r << 5) | c;
            }
        }
    }
    // Fisher-Yates シャッフル
    for (uint16_t i = pixel_count; i > 1; i--) {
        uint16_t j = random(i);
        uint16_t t = pixel_list[i - 1];
        pixel_list[i - 1] = pixel_list[j];
        pixel_list[j] = t;
    }
}

// ---- 実行状態 ----
static unsigned long boot_start_ms;
static bool se_boot_played;
static bool chime_played;
static bool se_hit_played;

void boot_init() {
    boot_start_ms   = millis();
    se_boot_played  = false;
    chime_played    = false;
    se_hit_played   = false;
    phase4_prepared = false;
    init_phase1();
    display_clear();
}

void boot_update() {
    unsigned long now = millis();
    uint16_t elapsed = (uint16_t)(now - boot_start_ms);
    if (elapsed >= PH4_END) return;

    display_clear();

    if (elapsed < PH1_END) {
        // Phase 1: ランダム → 文字位置へ線形補間
        for (uint8_t i = 0; i < NUM_PARTICLES; i++) {
            int16_t x = (int16_t)particles[i].sx +
                        ((int16_t)particles[i].tx - particles[i].sx) * (int32_t)elapsed / PH1_END;
            int16_t y = (int16_t)particles[i].sy +
                        ((int16_t)particles[i].ty - particles[i].sy) * (int32_t)elapsed / PH1_END;
            display_set(x, y, true);
        }
    } else if (elapsed < PH2_END) {
        // Phase 2: 光の帯スイープ + 全文字表示 + SE_BOOT
        if (!se_boot_played) {
            buzzer_play_se(SE_BOOT);
            se_boot_played = true;
        }
        draw_text();
        uint16_t t2 = elapsed - PH1_END;
        int16_t bar = (int16_t)(DISP_COLS - 1) * t2 / (PH2_END - PH1_END);
        for (uint8_t w = 0; w < BVS; w++) {            // 16 行版は 2 列幅の帯
            for (uint8_t r = 0; r < DISP_ROWS; r++) {
                display_set(bar - w, r, true);
            }
        }
    } else if (elapsed < PH3_END) {
        // Phase 3: 放射バースト (16 行版は二重リング) + 中間でチャイム
        if (elapsed >= PH3_MID && !chime_played) {
            buzzer_play_se(SE_CATCH);
            chime_played = true;
        }
        draw_text();
        uint16_t t3 = elapsed - PH2_END;
        int8_t radius = 1 + (int8_t)((int32_t)t3 * (BURST_MAX - 1) / (PH3_END - PH2_END));
        static const int8_t DIRS[8][2] = {
            { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1},
            {-1, 0}, {-1,-1}, { 0,-1}, { 1,-1},
        };
        const int8_t cx = 15;
        const int8_t cy = TEXT_ROW + GLYPH_H / 2;
        for (uint8_t i = 0; i < 8; i++) {
            display_set(cx + DIRS[i][0] * radius, cy + DIRS[i][1] * radius, true);
            if (BVS == 2) {                            // 内側リング (追従する核)
                int8_t inner = radius / 2;
                if (inner >= 1) {
                    display_set(cx + DIRS[i][0] * inner, cy + DIRS[i][1] * inner, true);
                }
            }
        }
    } else {
        // Phase 4: ランダム順に画素消灯 + SE_HIT
        if (!phase4_prepared) {
            draw_text();
            init_phase4();
            phase4_prepared = true;
        }
        if (!se_hit_played) {
            buzzer_play_se(SE_HIT);
            se_hit_played = true;
        }
        draw_text();
        uint16_t t4 = elapsed - PH3_END;
        uint16_t target_off = (uint16_t)((uint32_t)pixel_count * t4 / (PH4_END - PH3_END));
        if (target_off > pixel_count) target_off = pixel_count;
        for (uint16_t i = 0; i < target_off; i++) {
            uint16_t addr = pixel_list[i];
            display_set(addr & 0x1F, addr >> 5, false);
        }
    }
}

bool boot_is_finished() {
    return (millis() - boot_start_ms) >= PH4_END;
}
