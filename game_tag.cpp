#include "game_tag.h"
#include "display.h"
#include "buzzer.h"
#include "gyro.h"

#define PLAY_W          PLAY_COLS   // プレイエリア幅 = 31 (col 0-30, col 31 はゲージ)
#define PLAY_H          DISP_ROWS   // プレイエリア高さ (8 または 16)
#define GAME_DURATION_MS 30000UL    // 制限時間: 30秒

struct AIParams {
    uint8_t move_every_n; // N フレームに1回 NPC が動く
    uint8_t random_pct;   // ランダム移動の確率 (0-100)
    bool    use_bfs;      // true = BFS 距離マップ, false = Chebyshev 近似
};

static const AIParams PARAMS[3] = {
    {3, 40, false}, // EASY: のろい・よく迷う
    {2, 15, false}, // NORM: 速め・たまに迷う
    {1,  0, true }, // HARD: プレイヤーと同速・最適逃走
};

static uint8_t  px, py;         // プレイヤー位置
static uint8_t  nx, ny;         // NPC 位置
static uint8_t  score;
static bool     game_over;
static uint32_t frame_count;
static unsigned long game_start_ms;
static unsigned long remaining_ms;
static AIParams ai;

// ---- 捕獲アニメーション ----
// 0 = 非アクティブ
// 1 = フラッシュ (全点灯)
// 2〜4 = パーティクル放射 (radius 1→2→3)
static uint8_t  catch_anim_frame;
static uint8_t  catch_cx, catch_cy;

// ---- 警告演出 ----
static unsigned long last_warn_ms;

// BFS 用バッファ (ヒープ不使用)。キューはプレイエリア全セルを収容。
static uint8_t  dist[PLAY_H][PLAY_W];
static uint8_t  qx[PLAY_W * PLAY_H], qy[PLAY_W * PLAY_H];

static const int8_t DX[4] = { 1, -1,  0,  0};
static const int8_t DY[4] = { 0,  0,  1, -1};

// ---------- BFS: プレイヤー位置からの距離マップを生成 ----------
static void bfs_from_player() {
    memset(dist, 0xFF, sizeof(dist));
    uint16_t head = 0, tail = 0;   // 全セル数 (最大 31×16=496) を収容できる幅
    dist[py][px] = 0;
    qx[tail] = px;
    qy[tail++] = py;
    while (head != tail) {
        uint8_t x = qx[head], y = qy[head++];
        for (uint8_t d = 0; d < 4; d++) {
            int8_t cx = (int8_t)x + DX[d];
            int8_t cy = (int8_t)y + DY[d];
            if (cx < 0 || cx >= PLAY_W || cy < 0 || cy >= PLAY_H) continue;
            if (dist[cy][cx] != 0xFF) continue;
            dist[cy][cx] = dist[y][x] + 1;
            qx[tail] = (uint8_t)cx;
            qy[tail++] = (uint8_t)cy;
        }
    }
}

// Chebyshev 距離 (BFS 非使用時の近似)
static uint8_t chebyshev(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2) {
    uint8_t dx = (x1 > x2) ? x1 - x2 : x2 - x1;
    uint8_t dy = (y1 > y2) ? y1 - y2 : y2 - y1;
    return (dx > dy) ? dx : dy;
}

// ---------- NPC 移動 ----------
static void npc_step() {
    if (frame_count % ai.move_every_n != 0) return;

    // ランダム移動
    if ((uint8_t)random(100) < ai.random_pct) {
        uint8_t d = random(4);
        int8_t cx = (int8_t)nx + DX[d];
        int8_t cy = (int8_t)ny + DY[d];
        if (cx >= 0 && cx < PLAY_W && cy >= 0 && cy < PLAY_H) {
            nx = (uint8_t)cx;
            ny = (uint8_t)cy;
        }
        return;
    }

    // 最大距離方向へ移動
    if (ai.use_bfs) bfs_from_player();

    uint8_t best_val = ai.use_bfs ? dist[ny][nx]
                                  : chebyshev(nx, ny, px, py);
    int8_t  best_dx = 0, best_dy = 0;

    for (uint8_t d = 0; d < 4; d++) {
        int8_t cx = (int8_t)nx + DX[d];
        int8_t cy = (int8_t)ny + DY[d];
        if (cx < 0 || cx >= PLAY_W || cy < 0 || cy >= PLAY_H) continue;
        uint8_t val = ai.use_bfs ? dist[cy][cx]
                                 : chebyshev((uint8_t)cx, (uint8_t)cy, px, py);
        if (val > best_val) {
            best_val = val;
            best_dx  = DX[d];
            best_dy  = DY[d];
        }
    }
    nx = (uint8_t)((int8_t)nx + best_dx);
    ny = (uint8_t)((int8_t)ny + best_dy);
}

// ---------- NPC スポーン: プレイヤーから最も遠い角に出現 ----------
static void spawn_npc() {
    const uint8_t cx[4] = {0, PLAY_W - 1, 0,         PLAY_W - 1};
    const uint8_t cy[4] = {0, 0,          PLAY_H - 1, PLAY_H - 1};
    uint8_t best_corner = 0, best_dist = 0;
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t d = chebyshev(cx[i], cy[i], px, py);
        if (d > best_dist) { best_dist = d; best_corner = i; }
    }
    nx = cx[best_corner];
    ny = cy[best_corner];
}

// ---------- 公開関数 ----------
void tag_init(Difficulty diff) {
    ai               = PARAMS[diff];
    px               = PLAY_W / 2;
    py               = PLAY_H / 2;
    score            = 0;
    frame_count      = 0;
    game_over        = false;
    remaining_ms     = GAME_DURATION_MS;
    game_start_ms    = millis();
    catch_anim_frame = 0;
    last_warn_ms     = 0;
    spawn_npc();
    buzzer_play_bgm(BGM_TAG);
    buzzer_play_se(SE_START);
}

void tag_update() {
    if (game_over) return;
    frame_count++;

    // 残り時間更新
    unsigned long elapsed = millis() - game_start_ms;
    if (elapsed >= GAME_DURATION_MS) {
        game_over = true;
        buzzer_play_se(SE_GAMEOVER);
        display_clear();
        return;
    }
    remaining_ms = GAME_DURATION_MS - elapsed;

    // プレイヤー移動: ジャイロ傾き (3フレに1マス = 約10マス/秒)
    if (frame_count % 3 == 0) {
        int8_t tx = gyro_tilt_x(ORIENT_HORIZONTAL);
        int8_t ty = gyro_tilt_y(ORIENT_HORIZONTAL);
        if (tx < 0 && px > 0)          px--;
        if (tx > 0 && px < PLAY_W - 1) px++;
        if (ty < 0 && py > 0)          py--;
        if (ty > 0 && py < PLAY_H - 1) py++;
    }

    // NPC 移動
    npc_step();

    // 捕獲判定
    if (px == nx && py == ny) {
        score++;
        buzzer_play_se(SE_CATCH);
        catch_anim_frame = 1;
        catch_cx = px;
        catch_cy = py;
        spawn_npc();
    }

    // 描画開始
    display_clear();

    uint8_t gauge = (uint8_t)((remaining_ms * PLAY_H) / GAME_DURATION_MS);
    display_set_timer_gauge(gauge);

    // 捕獲アニメーション優先 (通常描画を上書き)
    if (catch_anim_frame > 0) {
        if (catch_anim_frame == 1) {
            // フレーム 1: プレイエリア全点灯 (フラッシュ)
            for (uint8_t r = 0; r < PLAY_H; r++) {
                for (uint8_t c = 0; c < PLAY_W; c++) {
                    display_set(c, r, true);
                }
            }
        } else {
            // フレーム 2-4: radius 1→2→3 の放射パーティクル
            int8_t radius = catch_anim_frame - 1;
            int16_t x, y;
            x = catch_cx + radius; if (x < PLAY_W) display_set(x, catch_cy, true);
            x = catch_cx - radius; if (x >= 0)     display_set(x, catch_cy, true);
            y = catch_cy + radius; if (y < PLAY_H) display_set(catch_cx, y, true);
            y = catch_cy - radius; if (y >= 0)     display_set(catch_cx, y, true);
        }
        catch_anim_frame++;
        if (catch_anim_frame > 4) catch_anim_frame = 0;
        return;
    }

    // 残り 5 秒以下: 全列警告フラッシュ (8f ON / 8f OFF) + 1s 毎に SE_WARN
    if (remaining_ms <= 5000) {
        if (millis() - last_warn_ms >= 1000) {
            buzzer_play_se(SE_WARN);
            last_warn_ms = millis();
        }
        if ((frame_count / 8) % 2 == 0) {
            for (uint8_t c = 0; c < PLAY_W; c++) {
                display_set(c, 0, true);
            }
        }
    }

    // NPC (逃げる子): 8Hz の速い点滅 → ビクビクして逃げている印象
    if ((frame_count / 4) % 2 == 0) {
        display_set(nx, ny, true);
    }

    // プレイヤー (鬼): 中心常時点灯 + 周囲 4 近傍を順繰り点灯 (回転ハロー)
    // → 「迫ってくる」迫力を出し、NPC との区別を明瞭にする
    display_set(px, py, true);
    static const int8_t HDX[4] = { 1, 0, -1, 0 };
    static const int8_t HDY[4] = { 0, 1,  0, -1 };
    uint8_t phase = (frame_count / 3) % 4;
    int8_t hx = (int8_t)px + HDX[phase];
    int8_t hy = (int8_t)py + HDY[phase];
    if (hx >= 0 && hx < PLAY_W && hy >= 0 && hy < PLAY_H) {
        display_set((uint8_t)hx, (uint8_t)hy, true);
    }
}

bool tag_is_over() {
    return game_over;
}

uint8_t tag_get_score() {
    return score;
}
