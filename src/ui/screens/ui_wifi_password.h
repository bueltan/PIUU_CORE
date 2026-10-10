/**
 * @file ui_wifi_password.h
 * @brief Password editor for the selected network. The keyboard edits uiModel().wifiDraft.password directly;
 * ui_controller processes confirmation.
 */

#pragma once
#include "ui/touch_phase.h"

#include <Arduino.h>

enum UiWifiPasswordAction
{
    UI_WIFI_PASSWORD_ACTION_NONE,
    UI_WIFI_PASSWORD_ACTION_BACK,
    UI_WIFI_PASSWORD_ACTION_OK
};

/**
 * @brief Binds the keyboard to uiModel().wifiDraft.password, resets case/symbol mode and sets maxLength to 64. Does
 * not clear the existing password.
 */
void uiWifiPasswordInit();
/**
 * @brief Draws the complete WiFi password screen using current application state.
 */
void uiWifiPasswordDrawBase();
/**
 * @brief Interprets pixel coordinates on the WiFi password screen and returns an action, or NONE
 * if no control matches.
 */
UiWifiPasswordAction uiWifiPasswordHandleTouch(uint16_t x, uint16_t y, TouchPhase phase);