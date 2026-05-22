#include "game_dodge.h"
#include "display.h"
#include "buzzer.h"
#include "config.h"
#include "gyro.h"

#define DODGE_W      DISP_ROWS    // 論理幅 (8 または 16)
#define DODGE_H      DISP_COLS    // 論理高さ (= 32)
#define PLAYER_Y     (DODGE_H - 2)
#define MAX_OBS       8
#define FALL_MIN_FR   3

struct Obstacle {
    int8_t  x;
    int8_t  y;
    uint8_t w;
    bool    active;
};

static Obstacle obs[MAX_OBS];

static uint8_t       cur_mode;
static uint8_t       px;
static uint32_t      frame_count;
static uint8_t       fall_frames;
static uint8_t       spawn_frames;
static unsigned long game_start_ms;
static unsigned long last_speedup_ms;

static bool     game_over;
static uint16_t final_score;

static void spawn_obs() {
    for (uint8_t i = 0; i < MAX_OBS; i++) {
        if (!obs[i].active) {
            uint8_t w = (uint8_t)random(DODGE_W * 3 / 8) + 1;   // 幅に比例 (8幅:1-3 / 16幅:1-6)
            obs[i].x      = (int8_t)random(DODGE_W - w + 1);
            obs[i].y      = 0;
            obs[i].w      = w;
            obs[i].active = true;
            return;
        }
    }
}

static void fall_obs() {
    for (uint8_t i = 0; i < MAX_OBS; i++) {
        if (!obs[i].active) continue;
        obs[i].y++;
        if (obs[i].y >= DODGE_H) obs[i].active = false;
    }
}

static bool collide() {
    for (uint8_t i = 0; i < MAX_OBS; i++) {
        if (!obs[i].active) continue;
        if (obs[i].y == PLAYER_Y && px >= obs[i].x && px < obs[i].x + obs[i].w) {
            return true;
        }
    }
    return false;
}

void dodge_init(uint8_t mode) {
    cur_mode    = mode;
    px          = DODGE_W / 2;
    frame_count = 0;
    for (uint8_t i = 0; i < MAX_OBS; i++) obs[i].active = false;

    if (cur_mode == DODGE_EASY) {
        fall_frames  = 10;
        spawn_frames = 20;
    } else {
        fall_frames  = 6;
        spawn_frames = 12;
    }

    game_over       = false;
    final_score     = 0;
    game_start_ms   = millis();
    last_speedup_ms = game_start_ms;

    display_set_orientation(ORIENT_VERTICAL);
    buzzer_play_bgm(BGM_DODGE);
    buzzer_play_se(SE_START);
}

void dodge_update() {
    if (game_over) return;
    frame_count++;

    // 左右移動: ジャイロ傾き (3フレに1マス = 約10マス/秒)
    if (frame_count % 3 == 0) {
        int8_t tx = gyro_tilt_x(ORIENT_VERTICAL);
        if (tx < 0 && px > 0)            px--;
        if (tx > 0 && px < DODGE_W - 1)  px++;
    }

    // 5 秒ごとに速度上昇
    if (millis() - last_speedup_ms >= 5000) {
        last_speedup_ms = millis();
        if (fall_frames > FALL_MIN_FR) fall_frames--;
    }

    if (frame_count % fall_frames  == 0) fall_obs();
    if (frame_count % spawn_frames == 0) spawn_obs();

    if (collide()) {
        game_over = true;
        uint16_t sec = (uint16_t)(frame_count / TARGET_FPS);
        final_score = (sec > 99) ? 99 : sec;
        buzzer_play_se(SE_HIT);
        return;
    }

    // 描画
    display_clear();
    for (uint8_t i = 0; i < MAX_OBS; i++) {
        if (!obs[i].active) continue;
        for (uint8_t w = 0; w < obs[i].w; w++) {
            display_set(obs[i].x + w, obs[i].y, true);
        }
    }
    // プレイヤー: 約 4Hz で点滅 (4フレーム間隔)
    if ((frame_count / 4) % 2 == 0) {
        display_set(px, PLAYER_Y, true);
    }
}

bool     dodge_is_over()    { return game_over; }
uint16_t dodge_get_score()  { return final_score; }
