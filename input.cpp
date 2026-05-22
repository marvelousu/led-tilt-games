#include "input.h"

static const uint8_t PIN[BTN_COUNT] = {2, 3, 4, 5, 6};

static bool    cur[BTN_COUNT];
static bool    prev[BTN_COUNT];
static uint8_t db[BTN_COUNT];          // デバウンスカウンタ
static uint8_t hold_frames[BTN_COUNT]; // 押し続けたフレーム数

#define DEBOUNCE_FRAMES       2   // 確定に必要なフレーム数 (~66ms)
#define REPEAT_START_FRAMES  15   // リピート開始までのフレーム数 (~500ms)
#define REPEAT_INTERVAL       3   // リピート間隔フレーム数 (~100ms = 10回/秒)

void input_init() {
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        pinMode(PIN[i], INPUT_PULLUP);
        cur[i] = prev[i] = false;
        db[i] = hold_frames[i] = 0;
    }
}

void input_update() {
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        prev[i] = cur[i];
        bool raw = (digitalRead(PIN[i]) == LOW);
        if (raw) {
            if (db[i] < DEBOUNCE_FRAMES) db[i]++;
        } else {
            db[i] = 0;
        }
        cur[i] = (db[i] >= DEBOUNCE_FRAMES);

        if (cur[i]) {
            if (hold_frames[i] < 255) hold_frames[i]++;
        } else {
            hold_frames[i] = 0;
        }
    }
}

bool input_pressed(Button b) {
    // 押した瞬間
    if (cur[b] && !prev[b]) return true;

    // オートリピート (CONFIRM は除く)
    if (b != BTN_CONFIRM && cur[b] && hold_frames[b] >= REPEAT_START_FRAMES) {
        return ((hold_frames[b] - REPEAT_START_FRAMES) % REPEAT_INTERVAL == 0);
    }
    return false;
}

bool input_held(Button b) {
    return cur[b];
}
