#include "config.h"
#include "display.h"
#include "input.h"
#include "buzzer.h"
#include "gyro.h"
#include "boot.h"
#include "menu.h"
#include "game_tag.h"
#include "game_sand.h"
#include "game_dodge.h"

// ---- ステートマシン ----
enum AppState {
    STATE_BOOT,
    STATE_MENU,
    STATE_VERTICAL_PROMPT,
    STATE_PLAYING_TAG,
    STATE_PLAYING_SAND,
    STATE_PLAYING_DODGE,
    STATE_GAMEOVER,
};

#define VERTICAL_PROMPT_MS   12000UL  // 回転検知/CONFIRM が主、これは保険のタイムアウト
#define GAMEOVER_AUTORET_MS  4000UL

static AppState      state          = STATE_BOOT;
static GameId        pending_game   = GAME_TAG;
static uint8_t       pending_mode   = 0;
static uint16_t      final_score    = 0;
static bool          score_rotated  = false;   // DODGE ゲームオーバー時 true
static unsigned long state_enter_ms = 0;

// ---- 縦持ち誘導画面: 回る円弧の矢印 + 横⇔縦に切り替わるデバイス ----
static void vertical_prompt_draw() {
    display_clear();
    const int16_t cx  = DISP_COLS / 2;
    const int16_t cy  = DISP_ROWS / 2;
    const bool    big = (DISP_ROWS >= 16);
    const int16_t r   = big ? 6 : 3;

    unsigned long t = millis() - state_enter_ms;

    // 中央のデバイス: 横長 ⇔ 縦長 を 0.6 秒ごとに切替 (90° 回転の表現)
    bool    portrait = ((t / 600) % 2) == 1;
    int16_t hw = portrait ? (big ? 2 : 1) : (big ? 4 : 2);
    int16_t hh = portrait ? (big ? 3 : 2) : 1;
    for (int16_t dx = -hw; dx <= hw; dx++) {
        display_set(cx + dx, cy - hh, true);
        display_set(cx + dx, cy + hh, true);
    }
    for (int16_t dy = -hh; dy <= hh; dy++) {
        display_set(cx - hw, cy + dy, true);
        display_set(cx + hw, cy + dy, true);
    }

    // 周囲をぐるぐる回る円弧の矢印
    float head = (float)((t / 60) % 24) * 15.0f;          // 先端角度 (度)
    for (int16_t k = 0; k < 12; k++) {                     // 弧本体
        float a = (head - (float)k * 14.0f) * 0.0174533f;
        display_set(cx + (int16_t)round(r * cos(a)),
                    cy + (int16_t)round(r * sin(a)), true);
    }
    float ba = (head - 22.0f) * 0.0174533f;                // 矢じり (クサビ)
    display_set(cx + (int16_t)round((r - 2) * cos(ba)),
                cy + (int16_t)round((r - 2) * sin(ba)), true);
    display_set(cx + (int16_t)round((r + 2) * cos(ba)),
                cy + (int16_t)round((r + 2) * sin(ba)), true);
}

// ---- ゲームオーバー画面: スコアを 2 桁で表示 ----
//   rotated=true の場合は縦向きオリエンテーションのまま論理座標で描画
//   16 行版は 2 倍サイズ (6×10) の数字を中央に置く
static void gameover_draw(uint16_t s, bool rotated) {
    display_clear();
    uint8_t tens = (uint8_t)((s / 10) % 10);
    uint8_t ones = (uint8_t)(s % 10);
    if (DISP_ROWS >= 16) {
        if (rotated) {
            // ORIENT_VERTICAL: 論理幅 16, 高さ 32
            display_draw_digit_scaled(5, 4,  tens, 2);
            display_draw_digit_scaled(5, 18, ones, 2);
        } else {
            display_draw_digit_scaled(8,  3, tens, 2);
            display_draw_digit_scaled(17, 3, ones, 2);
        }
    } else {
        if (rotated) {
            // ORIENT_VERTICAL: 論理幅 8, 高さ 32
            display_draw_digit(2, 10, tens);
            display_draw_digit(2, 18, ones);
        } else {
            display_draw_digit(12, 1, tens);
            display_draw_digit(17, 1, ones);
        }
    }
}

static void start_selected_game() {
    pending_game = menu_selected();
    pending_mode = menu_selected_mode();
    score_rotated = false;
    buzzer_stop();   // メニュー BGM を止める (各ゲームが必要なら自前で再開)

    if (pending_game == GAME_DODGE) {
        state          = STATE_VERTICAL_PROMPT;
        state_enter_ms = millis();
        gyro_rotate_reset();             // 縦持ち回転検知を開始
        return;
    }

    if (pending_game == GAME_TAG) {
        tag_init((Difficulty)pending_mode);
        state = STATE_PLAYING_TAG;
    } else { // GAME_SAND
        sand_init(pending_mode);
        state = STATE_PLAYING_SAND;
    }
    state_enter_ms = millis();
}

void setup() {
    randomSeed(analogRead(A0));
    display_init();
    input_init();
    buzzer_init();
    gyro_init();
    boot_init();
    state          = STATE_BOOT;
    state_enter_ms = millis();
}

void loop() {
    input_update();
    buzzer_update();
    gyro_update();

    switch (state) {
        case STATE_BOOT:
            boot_update();
            if (boot_is_finished()) {
                menu_init();
                state          = STATE_MENU;
                state_enter_ms = millis();
            }
            break;

        case STATE_MENU:
            menu_update();
            if (menu_confirmed()) {
                start_selected_game();
            }
            break;

        case STATE_VERTICAL_PROMPT:
            vertical_prompt_draw();
            if (millis() - state_enter_ms > VERTICAL_PROMPT_MS
                || input_pressed(BTN_CONFIRM)
                || gyro_rotate_detected()) {
                if (pending_game == GAME_DODGE) {
                    dodge_init(pending_mode);
                    state         = STATE_PLAYING_DODGE;
                    score_rotated = true;
                }
                state_enter_ms = millis();
            }
            break;

        case STATE_PLAYING_TAG:
            tag_update();
            if (tag_is_over()) {
                final_score    = tag_get_score();
                score_rotated  = false;
                state          = STATE_GAMEOVER;
                state_enter_ms = millis();
            }
            break;

        case STATE_PLAYING_SAND:
            sand_update();
            if (sand_wants_exit()) {
                // PLAY モード: CONFIRM で即メニュー復帰 (スコア表示なし)
                buzzer_stop();
                menu_init();
                state          = STATE_MENU;
                state_enter_ms = millis();
            } else if (sand_is_over()) {
                final_score    = sand_get_score();
                score_rotated  = false;
                state          = STATE_GAMEOVER;
                state_enter_ms = millis();
            }
            break;

        case STATE_PLAYING_DODGE:
            dodge_update();
            if (dodge_is_over()) {
                final_score    = dodge_get_score();
                score_rotated  = true;
                state          = STATE_GAMEOVER;
                state_enter_ms = millis();
            }
            break;

        case STATE_GAMEOVER:
            gameover_draw(final_score, score_rotated);
            if (millis() - state_enter_ms > GAMEOVER_AUTORET_MS
                || input_pressed(BTN_CONFIRM)) {
                buzzer_stop();
                display_set_orientation(ORIENT_HORIZONTAL);
                menu_init();
                state          = STATE_MENU;
                state_enter_ms = millis();
            }
            break;
    }

    display_update();
    delay(FRAME_MS);
}
