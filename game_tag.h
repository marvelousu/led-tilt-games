#pragma once
#include <Arduino.h>

enum Difficulty { DIFF_EASY, DIFF_NORM, DIFF_HARD };

void     tag_init(Difficulty diff);
void     tag_update();
bool     tag_is_over();
uint8_t  tag_get_score();
