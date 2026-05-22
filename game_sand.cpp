#include "game_sand.h"
#include "display.h"
#include "input.h"
#include "buzzer.h"
#include "gyro.h"

// ============================================================
//   game_sand.cpp : 粒子ベース砂シミュレーション
// ============================================================
// 各粒は (x, y) の sub-pixel 位置 + (vx, vy) の速度を持ち、重力で
// 加速しながら壁と他の粒に衝突する。セル単位では 1 セル = 1 粒の
// 排他占有で、ぶつかったらスライドまたは反発停止。Adafruit 方式。
// ------------------------------------------------------------

#define SAND_W  PLAY_COLS    // 31 (col 0-30)
#define SAND_H  DISP_ROWS    // 8 または 16 (ディスプレイ高さに追従)

#define KEEP_DURATION_MS       30000UL
#define KEEP_WARN_START_MS     10000UL
#define KEEP_WARN_INTERVAL_MS   3000UL

#define START_CUE_FRAMES          45
#define END_CUE_FRAMES            60

#define MAX_GRAINS          (SAND_H * 24)   // 粒配列上限   (8行:192 / 16行:384)
#define PLAY_INITIAL_GRAINS (SAND_H *  5)   // PLAY 初期粒数 (8行:40  / 16行:80)
#define TIME_FILL_TARGET    (SAND_H * 20)   // TIME クリア粒数 (8行:160 / 16行:320)
#define TIME_WALL_HITS_PER_SPAWN  25        // 壁衝突 25 回で 1 粒増殖
#define TIME_SHAKE_BURST    (SAND_H /  4)   // シェイク 1 回の追加粒数 (8行:2 / 16行:4)
#define TIME_SPAWN_COOLDOWN       10        // 増殖の最短間隔 (フレーム)

#define POS_SCALE                 16       // 1 セル = 16 sub-pixel
#define SUB_W    ((int16_t)SAND_W * POS_SCALE)
#define SUB_H    ((int16_t)SAND_H * POS_SCALE)
#define GRAV_A                     1       // 重力加速度 (sub-px / step^2)
#define MAX_VEL                   10       // 各軸最大速度 (< POS_SCALE 必須)

enum SandPhase {
    PHASE_START_CUE = 0,
    PHASE_PLAYING   = 1,
    PHASE_END_CUE   = 2,
};

struct Grain {
    int16_t x;    // sub-pixel 位置 [0, SUB_W)
    int16_t y;    // sub-pixel 位置 [0, SUB_H)
    int8_t  vx;
    int8_t  vy;
};

static Grain   grains[MAX_GRAINS];
static uint16_t grain_count;

// 占有グリッド (true = 粒が居る)。描画と衝突判定兼用。
static bool grid[SAND_H][SAND_W];

// パーティクル残光 (新粒生成時のハロー演出)
static bool    particle[SAND_H][SAND_W];
static uint8_t particle_frames;

static int8_t       gx_dir, gy_dir;
static uint8_t      cur_mode;
static SandPhase    phase;
static int16_t      phase_frames;
static bool         exit_req;
static bool         game_over;
static uint16_t     final_score;
static unsigned long play_start_ms;
static unsigned long last_warn_ms;
static uint16_t     time_wall_hits;
static uint8_t      time_spawn_cd;   // 増殖クールダウン

static bool in_cell(int8_t r, int8_t c) {
    return r >= 0 && r < SAND_H && c >= 0 && c < SAND_W;
}

// 新粒生成時の演出: 上下左右の 4 マスのうち空いているところに 1 フレームだけ残光
static void mark_halo(int8_t r, int8_t c) {
    static const int8_t CROSS4[4][2] = { {-1,0},{1,0},{0,-1},{0,1} };
    for (uint8_t i = 0; i < 4; i++) {
        int8_t pr = r + CROSS4[i][0];
        int8_t pc = c + CROSS4[i][1];
        if (in_cell(pr, pc) && !grid[pr][pc]) particle[pr][pc] = true;
    }
}

static bool add_grain_cell(int8_t r, int8_t c) {
    if (!in_cell(r, c)) return false;
    if (grain_count >= MAX_GRAINS) return false;
    if (grid[r][c]) return false;
    grains[grain_count].x  = (int16_t)c * POS_SCALE + POS_SCALE / 2;
    grains[grain_count].y  = (int16_t)r * POS_SCALE + POS_SCALE / 2;
    grains[grain_count].vx = 0;
    grains[grain_count].vy = 0;
    grid[r][c] = true;
    grain_count++;
    return true;
}

static void rebuild_grid() {
    memset(grid, 0, sizeof(grid));
    for (uint16_t i = 0; i < grain_count; i++) {
        uint8_t cx = grains[i].x / POS_SCALE;
        uint8_t cy = grains[i].y / POS_SCALE;
        grid[cy][cx] = true;
    }
}

// KEEP の「物理床」判定: 重力方向の壁だけ床 (非消失)、他は非床 (粒消滅)。
static bool lossy_left()   { return cur_mode == SAND_KEEP && gx_dir >= 0; }
static bool lossy_right()  { return cur_mode == SAND_KEEP && gx_dir <= 0; }
static bool lossy_top()    { return cur_mode == SAND_KEEP && gy_dir >= 0; }
static bool lossy_bottom() { return cur_mode == SAND_KEEP && gy_dir <= 0; }

static void physics_step() {
    if (grain_count == 0) return;
    rebuild_grid();

    // 粒の更新順をシャッフル (同じ方向に偏らないよう)
    for (uint16_t i = grain_count - 1; i > 0; i--) {
        uint16_t j = random(i + 1);
        if (j != i) {
            Grain t = grains[i]; grains[i] = grains[j]; grains[j] = t;
        }
    }

    bool count_wall = (cur_mode == SAND_TIME);

    for (uint16_t i = 0; i < grain_count; i++) {
        Grain& g = grains[i];

        int8_t old_cx = g.x / POS_SCALE;
        int8_t old_cy = g.y / POS_SCALE;
        grid[old_cy][old_cx] = false;   // 一時的に自セルを空ける

        // 重力適用 & 速度クランプ
        int16_t vx = (int16_t)g.vx + gx_dir * GRAV_A;
        int16_t vy = (int16_t)g.vy + gy_dir * GRAV_A;
        if (vx >  MAX_VEL) vx =  MAX_VEL;
        if (vx < -MAX_VEL) vx = -MAX_VEL;
        if (vy >  MAX_VEL) vy =  MAX_VEL;
        if (vy < -MAX_VEL) vy = -MAX_VEL;
        g.vx = (int8_t)vx; g.vy = (int8_t)vy;

        int16_t nx = g.x + g.vx;
        int16_t ny = g.y + g.vy;
        bool killed = false;
        bool wall_hit = false;

        // --- 壁衝突 (軸ごと) ---
        if (nx < 0) {
            if (lossy_left())  killed = true;
            else { nx = -nx; g.vx = -g.vx / 2; wall_hit = true; }
        } else if (nx >= SUB_W) {
            if (lossy_right()) killed = true;
            else { nx = 2 * (SUB_W - 1) - nx; g.vx = -g.vx / 2; wall_hit = true; }
        }
        if (!killed) {
            if (ny < 0) {
                if (lossy_top())    killed = true;
                else { ny = -ny; g.vy = -g.vy / 2; wall_hit = true; }
            } else if (ny >= SUB_H) {
                if (lossy_bottom()) killed = true;
                else { ny = 2 * (SUB_H - 1) - ny; g.vy = -g.vy / 2; wall_hit = true; }
            }
        }

        if (killed) {
            g.x = -1000;    // 死亡マーカー (後段で圧縮)
            continue;
        }
        if (wall_hit && count_wall) time_wall_hits++;

        int8_t new_cx = nx / POS_SCALE;
        int8_t new_cy = ny / POS_SCALE;

        // --- 粒同士の衝突 ---
        if (new_cx != old_cx || new_cy != old_cy) {
            if (grid[new_cy][new_cx]) {
                // 斜め移動: 片軸ずつスライドを試す
                if (new_cx != old_cx && !grid[old_cy][new_cx]) {
                    ny = g.y;
                    new_cy = old_cy;
                    g.vy = -g.vy / 2;
                } else if (new_cy != old_cy && !grid[new_cy][old_cx]) {
                    nx = g.x;
                    new_cx = old_cx;
                    g.vx = -g.vx / 2;
                } else {
                    // 全方向ブロック: 小反発して停止
                    nx = g.x; ny = g.y;
                    new_cx = old_cx; new_cy = old_cy;
                    g.vx = -g.vx / 3;
                    g.vy = -g.vy / 3;
                }
            }
        }

        g.x = nx;
        g.y = ny;
        grid[new_cy][new_cx] = true;
    }

    // 死亡粒を圧縮
    uint16_t w = 0;
    for (uint16_t r = 0; r < grain_count; r++) {
        if (grains[r].x > -500) {
            if (w != r) grains[w] = grains[r];
            w++;
        }
    }
    grain_count = w;
}

// ---------- TIME: 既存粒の隣に 1 粒生成 ----------
static bool spawn_near_existing() {
    if (grain_count == 0 || grain_count >= MAX_GRAINS) return false;
    uint16_t idx = random(grain_count);
    int8_t sr = grains[idx].y / POS_SCALE;
    int8_t sc = grains[idx].x / POS_SCALE;
    static const int8_t N8[8][2] = {
        {-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1},
    };
    uint8_t order[8] = {0,1,2,3,4,5,6,7};
    for (uint8_t i = 7; i > 0; i--) {
        uint8_t j = random(i + 1);
        uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
    }
    for (uint8_t i = 0; i < 8; i++) {
        int8_t nr = sr + N8[order[i]][0];
        int8_t nc = sc + N8[order[i]][1];
        if (add_grain_cell(nr, nc)) {
            mark_halo(nr, nc);
            if (particle_frames < 2) particle_frames = 2;
            return true;
        }
    }
    return false;
}

// ---------- 描画 ----------
static void draw_grid() {
    display_clear();
    for (uint8_t r = 0; r < SAND_H; r++) {
        for (uint8_t c = 0; c < SAND_W; c++) {
            if (grid[r][c]) display_set(c, r, true);
        }
    }
}

static void draw_full_on() {
    for (uint8_t r = 0; r < SAND_H; r++) {
        for (uint8_t c = 0; c < SAND_W; c++) {
            display_set(c, r, true);
        }
    }
}

static void draw_start_cue() {
    draw_grid();
    bool corners_on = ((phase_frames / 6) % 2) == 0;
    if (corners_on) {
        display_set(0,          0,          true);
        display_set(SAND_W - 1, 0,          true);
        display_set(0,          SAND_H - 1, true);
        display_set(SAND_W - 1, SAND_H - 1, true);
    }
}

static void draw_end_cue() {
    uint16_t elapsed = END_CUE_FRAMES - phase_frames;
    bool on = ((elapsed / 10) % 2) == 1;
    if (on) draw_full_on();
    else    draw_grid();
}

// ---------- 公開関数 ----------
void sand_init(uint8_t mode) {
    cur_mode = mode;
    grain_count = 0;
    memset(grid, 0, sizeof(grid));
    memset(particle, 0, sizeof(particle));
    particle_frames = 0;
    time_wall_hits  = 0;
    time_spawn_cd   = 0;

    if (mode == SAND_PLAY) {
        uint16_t placed = 0;
        while (placed < PLAY_INITIAL_GRAINS) {
            uint8_t r = random(SAND_H * 3 / 8);   // 上部 (8行:3行ぶん / 16行:6行ぶん)
            uint8_t c = random(SAND_W);
            if (add_grain_cell(r, c)) placed++;
        }
    } else if (mode == SAND_KEEP) {
        for (uint8_t r = 0; r < SAND_H / 2; r++) {
            for (uint8_t c = 0; c < SAND_W; c++) {
                add_grain_cell(r, c);
            }
        }
    } else { // SAND_TIME
        add_grain_cell(0, SAND_W / 2 - 1);
        add_grain_cell(0, SAND_W / 2);
        add_grain_cell(0, SAND_W / 2 + 1);
    }

    gx_dir = 0; gy_dir = 1;
    exit_req    = false;
    game_over   = false;
    final_score = 0;
    play_start_ms = millis();
    last_warn_ms  = play_start_ms;

    if (mode == SAND_PLAY) {
        phase = PHASE_PLAYING;
        phase_frames = 0;
    } else {
        phase = PHASE_START_CUE;
        phase_frames = START_CUE_FRAMES;
        buzzer_play_se(SE_WARN);
    }
}

void sand_update() {
    if (exit_req || game_over) return;

    if (input_pressed(BTN_CONFIRM)) {
        exit_req = true;
        return;
    }

    if (phase == PHASE_START_CUE) {
        draw_start_cue();
        phase_frames--;
        if (phase_frames == 10) buzzer_play_se(SE_START);
        if (phase_frames <= 0) {
            phase = PHASE_PLAYING;
            play_start_ms = millis();
            last_warn_ms  = play_start_ms;
        }
        return;
    }

    if (phase == PHASE_END_CUE) {
        draw_end_cue();
        phase_frames--;
        if (phase_frames <= 0) game_over = true;
        return;
    }

    // 重力方向 (斜めも許容): 傾きなしはデフォルトで下向き
    int8_t tx = gyro_tilt_x(ORIENT_HORIZONTAL);
    int8_t ty = gyro_tilt_y(ORIENT_HORIZONTAL);
    gx_dir = tx;
    gy_dir = (ty == 0) ? 1 : ty;

    // シェイク (KEEP=上半分補充、TIME=粒バースト、PLAY=無効)
    bool shook = gyro_shake_consumed();
    if (shook) {
        if (cur_mode == SAND_KEEP) {
            for (uint8_t r = 0; r < SAND_H / 2; r++) {
                for (uint8_t c = 0; c < SAND_W; c++) {
                    add_grain_cell(r, c);
                }
            }
        } else if (cur_mode == SAND_TIME) {
            for (uint8_t i = 0; i < TIME_SHAKE_BURST; i++) {
                spawn_near_existing();
            }
            buzzer_play_se(SE_CATCH);
        }
    }

    physics_step();
    physics_step();

    // TIME: 壁衝突蓄積で増殖 (クールダウン付き、1 フレーム 1 粒まで)
    if (cur_mode == SAND_TIME) {
        if (time_spawn_cd > 0) time_spawn_cd--;
        if (time_wall_hits >= TIME_WALL_HITS_PER_SPAWN
            && time_spawn_cd == 0
            && grain_count < MAX_GRAINS) {
            if (spawn_near_existing()) {
                time_wall_hits -= TIME_WALL_HITS_PER_SPAWN;
                time_spawn_cd = TIME_SPAWN_COOLDOWN;
            }
        }
        if (time_wall_hits > TIME_WALL_HITS_PER_SPAWN * 3) {
            time_wall_hits = TIME_WALL_HITS_PER_SPAWN * 3;   // 上限でクランプ
        }
        if (grain_count >= TIME_FILL_TARGET) {
            uint32_t sec = (millis() - play_start_ms) / 1000;
            if (sec > 99) sec = 99;
            final_score = (uint16_t)sec;
            phase = PHASE_END_CUE;
            phase_frames = END_CUE_FRAMES;
            buzzer_play_se(SE_CLEAR);
            return;
        }
    }

    // KEEP: タイマー + 警告
    if (cur_mode == SAND_KEEP) {
        unsigned long elapsed = millis() - play_start_ms;
        if (elapsed >= KEEP_DURATION_MS) {
            final_score = grain_count;
            phase = PHASE_END_CUE;
            phase_frames = END_CUE_FRAMES;
            buzzer_play_se(SE_GAMEOVER);
            return;
        }
        unsigned long remaining = KEEP_DURATION_MS - elapsed;
        if (remaining <= KEEP_WARN_START_MS) {
            if (millis() - last_warn_ms >= KEEP_WARN_INTERVAL_MS) {
                buzzer_play_se(SE_WARN);
                last_warn_ms = millis();
            }
        }
    }

    draw_grid();
    if (particle_frames > 0) {
        for (uint8_t r = 0; r < SAND_H; r++) {
            for (uint8_t c = 0; c < SAND_W; c++) {
                if (particle[r][c]) display_set(c, r, true);
            }
        }
        particle_frames--;
        if (particle_frames == 0) memset(particle, 0, sizeof(particle));
    }
}

bool     sand_wants_exit()  { return exit_req; }
bool     sand_is_over()     { return game_over; }
uint16_t sand_get_score()   { return final_score; }
