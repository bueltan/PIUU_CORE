/**
 * @file ui_setup.h
 * @brief WiFi, server and touch settings menu. Returns actions for the application controller to
 * process.
 */

#pragma once
#include "ui/touch_phase.h"

#include <Arduino.h>

enum UiSetupAction
{
    UI_SETUP_ACTION_NONE,
    UI_SETUP_ACTION_WIFI,
    UI_SETUP_ACTION_SERVER,
    UI_SETUP_ACTION_TOUCH,
    UI_SETUP_ACTION_BACK
};

/**
 * @brief Screen initialization hook; currently performs no operations.
 */
void uiSetupInit();
/**
 * @brief Draws the complete settings screen using current application state.
 */
void uiSetupDrawBase();
/**
 * @brief Interprets pixel coordinates on the settings screen and returns an action, or NONE if
 * no control matches.
 */
UiSetupAction uiSetupHandleTouch(uint16_t x, uint16_t y, TouchPhase phase);