/**
 * @file ui_keyboard.h
 * @brief Reusable keyboard with letters, symbols, case switching and deletion. Retains a pointer
 * to the active editor state, which must remain alive while in use.
 */

#pragma once
#include "ui/touch_phase.h"

#include <Arduino.h>

enum UiKeyboardAction
{
    UI_KEYBOARD_ACTION_NONE,
    UI_KEYBOARD_ACTION_CHANGED,
    UI_KEYBOARD_ACTION_OK
};

struct UiKeyboardState
{
    String *text;
    bool capsLock;
    bool symbolMode;
    size_t maxLength;
};

/**
 * @brief Associates the keyboard with an editor state and prepares buttons. The state pointer
 * must remain valid while the keyboard is used.
 */
void uiKeyboardInit(UiKeyboardState *state);
/**
 * @brief Redraws character keys and controls for the active keyboard mode. Does nothing without
 * an associated editor state.
 */
void uiKeyboardDraw();
/**
 * @brief Finds the touched key, draws its current phase and processes only release.
 * Cancel restores the key without editing. Returns NONE if no key or editor state matches.
 */
UiKeyboardAction uiKeyboardHandleTouch(uint16_t x, uint16_t y, TouchPhase phase);
/**
 * @brief Clamps the vertical offset to 0..10 pixels and invalidates geometry for the next button
 * initialization.
 */
void uiKeyboardSetVerticalOffset(int offsetY);