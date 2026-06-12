#include "buzzer.h"

// ============================================================
//   buzzer.cpp : BGM / SE データ + 再生
// ============================================================
//   曲は下記の *_SEQ[] 配列 (各 {周波数Hz, 長さms})。
//   編集方法・音域の制約 (全音 392Hz 以上) は docs/music_editing.md 参照。
// ------------------------------------------------------------

struct Note {
    uint16_t freq; // Hz (0 = 休符)
    uint16_t dur;  // ms
};

// ---- ゲーム BGM (ゲームごとに別) ----
//   Nano R4 の tone() は約 366Hz 未満でタイマー分周器が切り替わり音程が
//   ずれるため、BGM は全音 392Hz 以上に収めてある。
// 鬼ごっこ: skippy (付点リズム)
static const Note BGM_TAG_SEQ[] PROGMEM = {
    {392,230},{494,115},{587,230},{494,115},
    {523,230},{392,115},{440,345},
    {440,230},{523,115},{659,230},{523,115},
    {587,230},{440,115},{494,345},
    {523,230},{587,115},{659,230},{523,115},
    {494,230},{587,115},{392,345},
    {440,230},{523,115},{494,230},{440,115},
    {392,230},{494,115},{523,345},
};
static const uint8_t BGM_TAG_LEN = sizeof(BGM_TAG_SEQ) / sizeof(Note);

// 落下よけ: bright (なめらかな旋律)
static const Note BGM_DODGE_SEQ[] PROGMEM = {
    {392,175},{440,175},{523,175},{587,175},
    {659,175},{587,175},{523,350},
    {494,175},{523,175},{587,175},{659,175},
    {698,175},{659,175},{587,350},
    {523,175},{494,175},{440,175},{494,175},
    {523,175},{587,175},{659,350},
    {587,175},{523,175},{494,175},{440,175},
    {392,175},{440,175},{494,350},
};
static const uint8_t BGM_DODGE_LEN = sizeof(BGM_DODGE_SEQ) / sizeof(Note);

// ---- BGM: メニュー (C major, 約 7 秒) ----
//   休符・長音を排し全音符 220ms 均一。末尾 D→先頭 E が順次進行で繋がり、
//   ループ位置が分からないシームレスな流れになる。
static const Note BGM_MENU_SEQ[] PROGMEM = {
    {659,220},{784,220},{880,220},{784,220},   // E  G  A  G
    {659,220},{587,220},{523,220},{587,220},   // E  D  C  D
    {659,220},{784,220},{659,220},{523,220},   // E  G  E  C
    {587,220},{659,220},{587,220},{523,220},   // D  E  D  C
    {659,220},{784,220},{880,220},{1047,220},  // E  G  A  C6
    {988,220},{880,220},{784,220},{659,220},   // B  A  G  E
    {698,220},{659,220},{587,220},{659,220},   // F  E  D  E
    {523,220},{587,220},{659,220},{587,220},   // C  D  E  D
};
static const uint8_t BGM_MENU_LEN = sizeof(BGM_MENU_SEQ) / sizeof(Note);

// ---- SE: 捕まえた (上昇) ----
static const Note SE_CATCH_SEQ[] PROGMEM = {
    {523,  80}, {659,  80}, {784, 120},
};
static const uint8_t SE_CATCH_LEN = 3;

// ---- SE: ゲームオーバー (A5-F5-D5-B4 下降) ----
//   元は A4 始まりだったが 366Hz 未満 (F4/D4/B3) を含み音程が外れる
//   ため全体を 1 オクターブ上へ移調 (旋律の下降形はそのまま)。
static const Note SE_GAMEOVER_SEQ[] PROGMEM = {
    {880, 150}, {698, 150}, {587, 150}, {494, 300},
};
static const uint8_t SE_GAMEOVER_LEN = 4;

// ---- SE: スタート ----
static const Note SE_START_SEQ[] PROGMEM = {
    {523, 100}, {659, 100}, {784, 200},
};
static const uint8_t SE_START_LEN = 3;

// ---- SE: ブート (C5-E5-G5-C6-E6 上昇アルペジオ 計400ms) ----
static const Note SE_BOOT_SEQ[] PROGMEM = {
    {523, 80}, {659, 80}, {784, 80}, {1047, 80}, {1319, 80},
};
static const uint8_t SE_BOOT_LEN = 5;

// ---- SE: 警告 (1000Hz 短音 2 発 計 160ms) ----
static const Note SE_WARN_SEQ[] PROGMEM = {
    {1000, 60}, {0, 40}, {1000, 60},
};
static const uint8_t SE_WARN_LEN = 3;

// ---- SE: クリア (G5-C6-E6-G6 上昇 計 400ms) ----
static const Note SE_CLEAR_SEQ[] PROGMEM = {
    {784, 100}, {1047, 100}, {1319, 100}, {1568, 100},
};
static const uint8_t SE_CLEAR_LEN = 4;

// ---- SE: ヒット (E5→C5 下降 計 200ms) ----
//   元は E5→C4 だったが C4 (262Hz) は 366Hz 未満で音程が外れる
//   ため C5 へオクターブ上げ (下降の性格はそのまま)。
static const Note SE_HIT_SEQ[] PROGMEM = {
    {659, 100}, {523, 100},
};
static const uint8_t SE_HIT_LEN = 2;

// ---- SE: メニュー遷移 (B5→E6 短い上昇ブリップ 計 60ms) ----
static const Note SE_MENU_SEQ[] PROGMEM = {
    {988, 25}, {1319, 35},
};
static const uint8_t SE_MENU_LEN = 2;

// ---- 再生状態 ----
static const Note* bgm_seq  = nullptr;
static uint8_t     bgm_len  = 0;
static uint8_t     bgm_idx  = 0;

static const Note* se_seq   = nullptr;
static uint8_t     se_len   = 0;
static uint8_t     se_idx   = 0;
static bool        se_active = false;

static unsigned long note_start_ms = 0;
static uint16_t      note_dur_ms   = 0;

static void play_note(const Note* seq, uint8_t idx) {
    uint16_t freq = pgm_read_word(&seq[idx].freq);
    uint16_t dur  = pgm_read_word(&seq[idx].dur);
    note_dur_ms   = dur;
    note_start_ms = millis();
    if (freq == 0) {
        noTone(BUZZER_PIN);
    } else {
        tone(BUZZER_PIN, freq);
    }
}

void buzzer_init() {
    pinMode(BUZZER_PIN, OUTPUT);
}

void buzzer_stop() {
    noTone(BUZZER_PIN);
    bgm_seq  = nullptr;
    se_seq   = nullptr;
    se_active = false;
}

void buzzer_play_bgm(BGM bgm) {
    se_active = false;
    se_seq    = nullptr;
    if (bgm == BGM_NONE) { buzzer_stop(); return; }
    if (bgm == BGM_MENU) {
        bgm_seq = BGM_MENU_SEQ;   bgm_len = BGM_MENU_LEN;
    } else if (bgm == BGM_TAG) {
        bgm_seq = BGM_TAG_SEQ;    bgm_len = BGM_TAG_LEN;
    } else {  // BGM_DODGE
        bgm_seq = BGM_DODGE_SEQ;  bgm_len = BGM_DODGE_LEN;
    }
    bgm_idx = 0;
    play_note(bgm_seq, bgm_idx);
}

void buzzer_play_se(SoundEffect se) {
    switch (se) {
        case SE_CATCH:
            se_seq = SE_CATCH_SEQ;    se_len = SE_CATCH_LEN;    break;
        case SE_GAMEOVER:
            se_seq = SE_GAMEOVER_SEQ; se_len = SE_GAMEOVER_LEN; break;
        case SE_START:
            se_seq = SE_START_SEQ;    se_len = SE_START_LEN;    break;
        case SE_BOOT:
            se_seq = SE_BOOT_SEQ;     se_len = SE_BOOT_LEN;     break;
        case SE_WARN:
            se_seq = SE_WARN_SEQ;     se_len = SE_WARN_LEN;     break;
        case SE_CLEAR:
            se_seq = SE_CLEAR_SEQ;    se_len = SE_CLEAR_LEN;    break;
        case SE_HIT:
            se_seq = SE_HIT_SEQ;      se_len = SE_HIT_LEN;      break;
        case SE_MENU:
            se_seq = SE_MENU_SEQ;     se_len = SE_MENU_LEN;     break;
    }
    se_idx    = 0;
    se_active = true;
    play_note(se_seq, se_idx);
}

void buzzer_update() {
    if (millis() - note_start_ms < note_dur_ms) return;

    if (se_active && se_seq) {
        se_idx++;
        if (se_idx < se_len) {
            play_note(se_seq, se_idx);
        } else {
            // SE 終了 → BGM 再開
            se_active = false;
            se_seq    = nullptr;
            if (bgm_seq) {
                play_note(bgm_seq, bgm_idx);
            } else {
                noTone(BUZZER_PIN);
            }
        }
    } else if (bgm_seq) {
        bgm_idx = (bgm_idx + 1) % bgm_len;
        play_note(bgm_seq, bgm_idx);
    }
}
