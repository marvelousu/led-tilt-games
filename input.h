#pragma once
#include <Arduino.h>

enum Button {
    BTN_UP = 0,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_CONFIRM,
    BTN_COUNT
};

void input_init();
void input_update();           // 毎フレーム1回呼ぶ
bool input_pressed(Button b);  // 押した瞬間のみ true
bool input_held(Button b);     // 押し続けている間 true
