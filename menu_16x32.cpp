#include "config.h"

#ifdef MATRIX_EXT

// ============================================================
//   menu_16x32.cpp : 16×32 版メニュー (左=アイコン / 右=デモアニメ)
// ============================================================
//   左パネル row 0-6   : ゲームピクトグラム (14×7)
//   左パネル row 8-12  : モード番号 (数字 1〜)
//   左パネル row 14-15 : モードインジケーター (ドット, 選択中は背高)
//   右パネル col 16-31 : 選択中ゲームの簡易ループデモアニメ (16×16)
//   上下傾け=ゲーム切替 / 左右傾け=モード切替 (遷移時に SE)
// ------------------------------------------------------------

#include "menu.h"
#include "display.h"
#include "input.h"
#include "gyro.h"
#include "buzzer.h"
#include "game_sand.h"   // SandMode (SAND_PLAY / SAND_KEEP / SAND_TIME)

static const uint8_t MODE_COUNT[GAME_COUNT]  = { 3, 3, 2 };
static const bool    IS_VERTICAL[GAME_COUNT] = { false, false, true };

// ---- ピクトグラム (14列 × 7行, 行ごとに uint16_t, bit c = col c) ----
// TAG: 2 体のスティックフィギュア
static const uint16_t ICON_TAG[7] PROGMEM = {
    0x0C0C, 0x1E1E, 0x0C0C, 0x3F3F, 0x0C0C, 0x1212, 0x3333,
};
// SAND: 砂時計
static const uint16_t ICON_SAND[7] PROGMEM = {
    0x3FFF, 0x1FFE, 0x07F8, 0x01E0, 0x07F8, 0x1FFE, 0x3FFF,
};
// DODGE: 落下ブロック 3 つ + プレイヤーバー
static const uint16_t ICON_DODGE[7] PROGMEM = {
    0x000E, 0x0E0E, 0x0E00, 0x00E0, 0x00E0, 0x0000, 0x1FFE,
};

static const uint16_t* const ICONS[GAME_COUNT] = { ICON_TAG, ICON_SAND, ICON_DODGE };

#define ICON_W    14
#define ICON_H     7
#define ICON_COL   1
#define ICON_ROW   0

#define MODE_NUM_X   6      // モード番号 (3×5 数字) の左上 col
#define MODE_NUM_Y   8      // 同 row (rows 8-12)
#define MODE_DOT_SEL 14     // モードドット: 選択中の上段 row
#define MODE_DOT_BASE 15    // モードドット: 全ドット共通 row

// 右パネル (デモ領域)
#define DEMO_X0  16
#define DEMO_W   16
#define DEMO_H   16
#define DEMO_STEP 151  // TIME デモの散布ステップ (DEMO_W*DEMO_H=256 と互いに素)

static GameId  cur_game = GAME_TAG;
static uint8_t cur_mode[GAME_COUNT] = { 0, 0, 0 };
static bool    confirmed = false;
// エッジ検出用: 前フレームの傾き値 (0 → ±1 への立ち上がりで 1 ステップ操作)
static int8_t  last_tilt_x = 0;
static int8_t  last_tilt_y = 0;

// デモアニメ用フレームカウンタ
static uint16_t demo_anim = 0;

// SAND デモ: 落下する砂粒
#define SAND_GRAINS 20
static uint8_t sgx[SAND_GRAINS];
static uint8_t sgy[SAND_GRAINS];

// ---------- 左パネル ----------
static void draw_icon() {
    const uint16_t* icon = ICONS[cur_game];
    for (uint8_t r = 0; r < ICON_H; r++) {
        uint16_t rowbits = pgm_read_word(&icon[r]);
        for (uint8_t c = 0; c < ICON_W; c++) {
            if ((rowbits >> c) & 1) display_set(ICON_COL + c, ICON_ROW + r, true);
        }
    }
}

// モード番号 (1 始まり) を数字で表示 → モードインジケーターの上
static void draw_mode_number() {
    display_draw_digit(MODE_NUM_X, MODE_NUM_Y, cur_mode[cur_game] + 1);
}

static void draw_mode_dots() {
    uint8_t count = MODE_COUNT[cur_game];
    uint8_t sel   = cur_mode[cur_game];
    for (uint8_t i = 0; i < count; i++) {
        uint8_t col = 3 + i * 4;                       // 3, 7, 11
        display_set(col,     MODE_DOT_BASE, true);
        display_set(col + 1, MODE_DOT_BASE, true);
        if (i == sel) {                                // 選択中は 2 段で強調
            display_set(col,     MODE_DOT_SEL, true);
            display_set(col + 1, MODE_DOT_SEL, true);
        }
    }
}

// ---------- 右パネル (デモ) ----------
// デモ領域内の 1 ピクセル描画 (X 方向はラップ)
static void demo_px(int8_t lx, int8_t ly) {
    if (ly < 0 || ly >= DEMO_H) return;
    lx = ((lx % DEMO_W) + DEMO_W) % DEMO_W;
    display_set(DEMO_X0 + lx, ly, true);
}

// TAG: 鬼が子を追いかけて周回
static void draw_demo_tag() {
    uint8_t bob    = (demo_anim / 5) % 2;
    int8_t  runner = (demo_anim / 2) % DEMO_W;
    int8_t  chaser = runner - 5;

    int8_t ry = 4 + bob;                               // 子 (細身)
    demo_px(runner + 1, ry);
    for (uint8_t dx = 0; dx < 3; dx++) demo_px(runner + dx, ry + 1);
    demo_px(runner + 1, ry + 2);
    demo_px(runner + 1, ry + 3);
    demo_px(runner,     ry + 4); demo_px(runner + 2, ry + 4);
    demo_px(runner,     ry + 5); demo_px(runner + 2, ry + 5);

    int8_t cy = 4 + (1 - bob);                         // 鬼 (太め, 逆位相)
    for (uint8_t dy = 0; dy < 3; dy++)
        for (uint8_t dx = 0; dx < 3; dx++) demo_px(chaser + dx, cy + dy);
    demo_px(chaser + 1, cy + 3);
    demo_px(chaser,     cy + 4); demo_px(chaser + 2, cy + 4);
    demo_px(chaser,     cy + 5); demo_px(chaser + 2, cy + 5);
}

// ---- SAND デモ (モード別) ----
// PLAY (自由): 砂粒が落下し続ける (粒ごとに速度差)
static void draw_demo_sand_play() {
    for (uint8_t i = 0; i < SAND_GRAINS; i++) {
        uint8_t period = 2 + (i % 3);
        if (demo_anim % period == 0) {
            sgy[i]++;
            if (sgy[i] >= DEMO_H) { sgy[i] = 0; sgx[i] = random(DEMO_W); }
        }
        demo_px(sgx[i], sgy[i]);
    }
}

// KEEP: 下の砂山を維持 — 上から補充され、端からこぼれ落ちる
static void draw_demo_sand_keep() {
    uint8_t cx    = DEMO_W / 2;
    uint8_t pileH = (DEMO_H >= 12) ? 5 : 3;
    for (uint8_t ry = 0; ry < pileH; ry++) {            // 砂山 (三角)
        uint8_t hw = ((pileH - ry) * DEMO_W) / (pileH * 3);
        for (int8_t c = (int8_t)cx - hw; c <= (int8_t)cx + hw; c++)
            demo_px(c, DEMO_H - 1 - ry);
    }
    for (uint8_t i = 0; i < 3; i++) {                   // 上から補充される砂
        uint8_t gy = (demo_anim / 3 + i * 5) % (DEMO_H - pileH);
        demo_px(cx - 2 + i * 2, gy);
    }
    for (uint8_t i = 0; i < 2; i++) {                   // 端からこぼれ落ちる砂
        uint8_t t   = (demo_anim / 2 + i * 8) % 16;
        int8_t  dir = (i == 0) ? -1 : 1;
        demo_px(cx + dir * (DEMO_W / 4 + t / 5), (DEMO_H - pileH) + t / 3);
    }
}

// TIME: 粒が増殖して画面を埋め、満杯でリセット
static void draw_demo_sand_time() {
    uint16_t total   = (uint16_t)DEMO_W * DEMO_H;
    uint16_t fillMax = total * 3 / 4;
    uint16_t n = 4 + (demo_anim * 2) % fillMax;
    for (uint16_t i = 0; i < n; i++) {
        uint16_t cell = (uint16_t)(((uint32_t)i * DEMO_STEP) % total);
        demo_px(cell % DEMO_W, cell / DEMO_W);
    }
}

// 選択中の SAND モードでデモを切替
static void draw_demo_sand() {
    switch (cur_mode[GAME_SAND]) {
        case SAND_KEEP: draw_demo_sand_keep(); break;
        case SAND_TIME: draw_demo_sand_time(); break;
        default:        draw_demo_sand_play(); break;
    }
}

// DODGE: ブロックが落下、プレイヤーバーが左右に回避
static void draw_demo_dodge() {
    static const uint8_t lane[3] = { 2, 7, 12 };
    for (uint8_t b = 0; b < 3; b++) {
        int8_t by = (int8_t)((demo_anim / 3 + b * 5) % 19) - 3;
        for (uint8_t dy = 0; dy < 3; dy++)
            for (uint8_t dx = 0; dx < 3; dx++)
                demo_px(lane[b] + dx, by + dy);
    }
    uint8_t ph  = (demo_anim / 4) % 20;
    uint8_t off = (ph < 10) ? ph : 20 - ph;            // 0→10→0 の三角波
    for (uint8_t dx = 0; dx < 5; dx++)
        demo_px(2 + off / 2 + dx, DEMO_H - 1);
}

static void draw_demo() {
    if      (cur_game == GAME_TAG)  draw_demo_tag();
    else if (cur_game == GAME_SAND) draw_demo_sand();
    else                            draw_demo_dodge();
}

// ---------- 公開関数 ----------
void menu_init() {
    cur_game  = GAME_TAG;
    confirmed = false;
    // 現在の傾きを初期値とする → ゲーム復帰時に傾いたままでも誤作動しない
    last_tilt_x = gyro_tilt_x(ORIENT_HORIZONTAL);
    last_tilt_y = gyro_tilt_y(ORIENT_HORIZONTAL);
    demo_anim = 0;
    for (uint8_t i = 0; i < SAND_GRAINS; i++) {
        sgx[i] = random(DEMO_W);
        sgy[i] = random(DEMO_H);
    }
    // cur_mode は前回選択を維持
    buzzer_play_bgm(BGM_MENU);
}

void menu_update() {
    confirmed = false;
    demo_anim++;

    int8_t tx = gyro_tilt_x(ORIENT_HORIZONTAL);
    int8_t ty = gyro_tilt_y(ORIENT_HORIZONTAL);

    // 左右傾き: モード切替
    uint8_t mc = MODE_COUNT[cur_game];
    if (tx != 0 && last_tilt_x == 0) {
        if (tx < 0) cur_mode[cur_game] = (cur_mode[cur_game] + mc - 1) % mc;
        else        cur_mode[cur_game] = (cur_mode[cur_game] + 1)      % mc;
        buzzer_play_se(SE_MENU);
    }

    // 前後傾き: ゲーム切替
    if (ty != 0 && last_tilt_y == 0) {
        if (ty < 0) cur_game = (GameId)((cur_game + GAME_COUNT - 1) % GAME_COUNT);
        else        cur_game = (GameId)((cur_game + 1)              % GAME_COUNT);
        buzzer_play_se(SE_MENU);
    }

    last_tilt_x = tx;
    last_tilt_y = ty;

    if (input_pressed(BTN_CONFIRM)) confirmed = true;

    display_clear();
    draw_icon();
    draw_mode_number();
    draw_mode_dots();
    draw_demo();
}

bool    menu_confirmed()            { return confirmed; }
GameId  menu_selected()             { return cur_game; }
uint8_t menu_selected_mode()        { return cur_mode[cur_game]; }
bool    menu_selected_is_vertical() { return IS_VERTICAL[cur_game]; }
uint8_t menu_mode_count(GameId g)   { return MODE_COUNT[g]; }

#endif // MATRIX_EXT
