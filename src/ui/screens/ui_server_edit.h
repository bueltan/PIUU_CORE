/**
 * @file ui_server_edit.h
 * @brief Server address editor using the shared keyboard. Edits uiModel().serverDraft directly; navigating
 * back does not automatically restore its previous value.
 */

#pragma once
#include "ui/touch_phase.h"

#include <Arduino.h>

enum UiServerEditAction
{
    UI_SERVER_EDIT_ACTION_NONE,
    UI_SERVER_EDIT_ACTION_BACK,
    UI_SERVER_EDIT_ACTION_OK
};

/**
 * @brief Binds the keyboard to uiModel().serverDraft, resets case/symbol mode and sets maxLength to 64. Does
 * not copy or restore the previous address.
 */
void uiServerEditInit();
/**
 * @brief Draws the complete server address editor screen using current application state.
 */
void uiServerEditDrawBase();
/**
 * @brief Interprets pixel coordinates on the server address editor screen and returns an action,
 * or NONE if no control matches.
 */
UiServerEditAction uiServerEditHandleTouch(uint16_t x, uint16_t y, TouchPhase phase);