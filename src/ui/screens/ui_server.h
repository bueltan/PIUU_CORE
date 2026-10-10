/**
 * @file ui_server.h
 * @brief Saved server list and select, add, edit and delete controls. Returns actions to the
 * application controller.
 */

#pragma once
#include "ui/touch_phase.h"

#include <Arduino.h>

enum UiServerAction
{
    UI_SERVER_ACTION_NONE,
    UI_SERVER_ACTION_SELECT,
    UI_SERVER_ACTION_ADD,
    UI_SERVER_ACTION_EDIT,
    UI_SERVER_ACTION_DELETE,
    UI_SERVER_ACTION_OK,
    UI_SERVER_ACTION_BACK
};

/**
 * @brief Screen initialization hook; currently performs no operations.
 */
void uiServerInit();
/**
 * @brief Draws the complete server list screen using current application state.
 */
void uiServerDrawBase();
/**
 * @brief Redraws entries on the server list screen, including the current selection highlight.
 */
void uiServerRedrawList();
/**
 * @brief Interprets pixel coordinates on the server list screen and returns an action, or NONE
 * if no control matches.
 * @param[out] selectedIndex Selected row for a SELECT action; otherwise -1.
 */
UiServerAction uiServerHandleTouch(uint16_t x, uint16_t y, int &selectedIndex, TouchPhase phase);