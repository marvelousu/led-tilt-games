#pragma once
#include <Arduino.h>

// ============================================================
//   game_dodge.h : 落下よけ (縦持ち)
// ============================================================
// 論理座標: 幅 DISP_ROWS (8 または 16), 高さ DISP_COLS (32)
// プレイヤーは下端付近に居座り、LEFT/RIGHT または UP/DOWN で左右移動
// 上から 1〜3 セル幅の障害物が落下、プレイヤーに触れたら終了
// ------------------------------------------------------------

enum DodgeMode {
    DODGE_EASY = 0,
    DODGE_HARD = 1,
};

void     dodge_init(uint8_t mode);
void     dodge_update();
bool     dodge_is_over();
uint16_t dodge_get_score();    // 生存秒数 (最大 99)
