#pragma once
#include <Arduino.h>
#include "config.h"

enum SoundEffect {
    SE_CATCH,     // NPC を捕まえた: 上昇音
    SE_GAMEOVER,  // ゲームオーバー: 下降音
    SE_START,     // ゲーム開始: ファンファーレ
    SE_BOOT,      // 起動アニメ: 5音上昇アルペジオ
    SE_WARN,      // 警告: 1000Hz 短音 2発
    SE_CLEAR,     // クリア: 4音上昇
    SE_HIT,       // 衝突: E5→C4 下降
    SE_MENU,      // メニュー遷移: 短い上昇ブリップ
};

enum BGM {
    BGM_NONE,
    BGM_TAG,      // 鬼ごっこ
    BGM_DODGE,    // 落下よけ
    BGM_MENU,
};

void buzzer_init();
void buzzer_update();                // 毎フレーム1回呼ぶ
void buzzer_play_se(SoundEffect se); // SE 再生 (終了後 BGM 再開)
void buzzer_play_bgm(BGM bgm);       // BGM 開始
void buzzer_stop();                  // 全停止
