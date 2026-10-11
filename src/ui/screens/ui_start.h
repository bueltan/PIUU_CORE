/**
 * @file ui_start.h
 * @brief Home screen with microphone control, setup access and READY, LISTENING, THINKING,
 * SPEAKING and ERROR states.
 */

#pragma once
#include "ui/touch_phase.h"

#include <Arduino.h>

enum UiState
{
    UI_READY,
    UI_LISTENING,
    UI_CONNECTING,
    UI_THINKING,
    UI_SPEAKING,
    UI_ERROR
};

enum UiStartAction
{
    UI_ACTION_NONE,
    UI_ACTION_MIC,
    UI_ACTION_SETUP
};

/**
 * @brief Initializes the home-screen visual state to UI_READY.
 */
void uiStartInit();
/**
 * @brief Draws the complete home screen using current application state.
 */
void uiStartDrawBase();
/**
 * @brief Updates status text, icons and button appearance. Even when state is unchanged,
 * refreshes connectivity, error text and microphone control.
 */
void uiStartApplyState(UiState state);
/**
 * @brief Redraws connectivity indicators on the home screen.
 */
void uiStartDrawWifiStatus();
/**
 * @brief Interprets pixel coordinates on the home screen and returns an action, or NONE if no
 * control matches.
 */
UiStartAction uiStartHandleTouch(uint16_t x, uint16_t y, TouchPhase phase);
/**
 * @brief Returns the most recently applied home-screen visual state.
 */
UiState uiStartGetState();