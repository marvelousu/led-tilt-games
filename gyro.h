#pragma once
#include <Arduino.h>
#include "display.h"

// ============================================================
//   gyro.h : LSM6DSV16X (Qwiic, I2C) 読み取りラッパー
// ============================================================
// 仕様書 NFR-MAINT-02 の「ジャイロ入力の置換点」を集約。
// 各ゲームは gyro_tilt_x / y / shake_consumed のみを呼ぶ。
// 物理軸⇔論理軸の回転は Orientation を渡して gyro 側で吸収。
// ------------------------------------------------------------

void   gyro_init();
void   gyro_update();

int8_t gyro_tilt_x(Orientation o);    // 左=-1, 無=0, 右=+1
int8_t gyro_tilt_y(Orientation o);    // 上=-1, 無=0, 下=+1
bool   gyro_shake_consumed();         // 立ち上がりを1回だけ返す

void   gyro_rotate_reset();           // 縦持ち回転検知を開始 (累積をリセット)
bool   gyro_rotate_detected();        // 約 90° 回転したら true

bool   gyro_is_ready();
