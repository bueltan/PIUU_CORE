/**
 * @file ui_wifi.h
 * @brief Discovered network list, RSSI display and selection. Scanning and connection are
 * coordinated outside this view.
 */

#pragma once
#include "ui/touch_phase.h"

#include <Arduino.h>

enum UiWifiAction
{
    UI_WIFI_ACTION_NONE,
    UI_WIFI_ACTION_SCAN,
    UI_WIFI_ACTION_BACK,
    UI_WIFI_ACTION_SELECT_NETWORK
};

/**
 * @brief Screen initialization hook; currently performs no operations.
 */
void uiWifiInit();
/**
 * @brief Draws the complete WiFi network list screen using current application state.
 */
void uiWifiDrawBase();
/**
 * @brief Redraws entries on the WiFi network list screen, including the current selection
 * highlight.
 */
void uiWifiRedrawList();
/**
 * @brief Interprets pixel coordinates on the WiFi network list screen and returns an action, or
 * NONE if no control matches.
 * @param[out] selectedIndex Selected row for a SELECT action; otherwise -1.
 */
UiWifiAction uiWifiHandleTouch(uint16_t x, uint16_t y, int &selectedIndex, TouchPhase phase);