#pragma once
#include <Arduino.h>
#include "ui/touch_phase.h"
enum UiVolumeAction { UI_VOLUME_NONE, UI_VOLUME_DOWN, UI_VOLUME_UP, UI_VOLUME_MUTE, UI_VOLUME_BACK };
void uiVolumeDrawBase();
UiVolumeAction uiVolumeHandleTouch(uint16_t x, uint16_t y, TouchPhase phase);
