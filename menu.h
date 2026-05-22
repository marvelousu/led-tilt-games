#pragma once
#include <Arduino.h>

// ============================================================
//   menu.h : ゲーム選択 + モード選択メニュー API
// ============================================================

enum GameId {
    GAME_TAG   = 0,   // 鬼ごっこ (横)
    GAME_SAND  = 1,   // 砂あそび (横)
    GAME_DODGE = 2,   // 落下よけ (縦)
    GAME_COUNT
};

void    menu_init();
void    menu_update();                // 毎フレーム呼ぶ (描画含む)

bool    menu_confirmed();             // CONFIRM 押下フレームで true
GameId  menu_selected();              // 現在選択中のゲーム
uint8_t menu_selected_mode();         // 現在のゲームのモード (0-indexed)
bool    menu_selected_is_vertical();  // 現在選択中のゲームが縦持ちか
uint8_t menu_mode_count(GameId g);    // ゲームのモード数
