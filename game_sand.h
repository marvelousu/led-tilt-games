#pragma once
#include <Arduino.h>

// ============================================================
//   game_sand.h : 砂あそび (3 モード)
// ============================================================

enum SandMode {
    SAND_PLAY = 0,   // 制限なし、LEFT/RIGHT/UP で重力方向変更、CONFIRM で終了
    SAND_KEEP = 1,   // 30 秒、画面端に達した砂は消滅、残砂数がスコア
    SAND_TIME = 2,   // row 4 仕切り + 穴 (1.5s ごと往復)、全砂下半分到達でクリア
};

void     sand_init(uint8_t mode);
void     sand_update();
bool     sand_wants_exit();   // CONFIRM → メニュー即復帰
bool     sand_is_over();      // タイマー切れ / クリア時 true
uint16_t sand_get_score();    // KEEP: 残砂数, TIME: 経過時間 (0.1s 単位)
