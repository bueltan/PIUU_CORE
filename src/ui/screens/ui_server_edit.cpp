#include "ui/touch_phase.h"
/**
 * @file ui_server_edit.cpp
 * @brief Server address editor using the shared keyboard. Edits uiModel().serverDraft directly; navigating
 * back does not automatically restore its previous value.
 */

#include "ui/screens/ui_server_edit.h"

#include <TFT_eSPI.h>

#include "ui/ui_model.h"
#include "config/settings.h"
#include "hardware/display_hal.h"
#include "ui/widgets/ui_keyboard.h"

static UiKeyboardState keyboardState;
static bool keyboardReady = false;

static constexpr int BACK_X = 8;
static constexpr int BACK_Y = 8;
static constexpr int BACK_W = 28;
static constexpr int BACK_H = 28;

static const uint8_t backArrow16x16[] = {
    0b00000000, 0b00000000,
    0b00000001, 0b00000000,
    0b00000011, 0b00000000,
    0b00000111, 0b00000000,
    0b00001111, 0b11111110,
    0b00011111, 0b11111110,
    0b00111111, 0b11111110,
    0b01111111, 0b11111110,
    0b00111111, 0b11111110,
    0b00011111, 0b11111110,
    0b00001111, 0b11111110,
    0b00000111, 0b00000000,
    0b00000011, 0b00000000,
    0b00000001, 0b00000000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000};

/**
 * @brief Draws this screen's title and associated header controls.
 */
static void drawTitle()
{
    TFT_eSPI &tft = display();

    screenLock();
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(54, 10);
    tft.print("SERVER IP");

    tft.drawRoundRect(BACK_X, BACK_Y, BACK_W, BACK_H, 4, TFT_GREEN);
    tft.drawBitmap(BACK_X + 6, BACK_Y + 6, backArrow16x16, 16, 16, TFT_GREEN);
    screenUnlock();
}

/**
 * @brief Redraws the current editor value, trimming visible text to the available space.
 */
static void drawHeader()
{
    TFT_eSPI &tft = display();

    String shown = uiModel().serverDraft;
    if (shown.length() > 48)
    {
        shown = shown.substring(shown.length() - 48);
    }

    screenLock();
    tft.fillRect(0, 36, 320, 38, TFT_BLACK);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, 38);
    tft.print(uiModel().settingsNotice.length() ? uiModel().settingsNotice.c_str() : "Server:");

    tft.drawRect(10, 50, 300, 24, TFT_GREEN);
    tft.setCursor(14, 58);
    tft.print(shown);
    screenUnlock();
}

/**
 * @brief Returns whether the pixel coordinates fall inside the back-button rectangle.
 */
static bool isBackTouched(uint16_t x, uint16_t y)
{
    return x >= BACK_X &&
           x < (BACK_X + BACK_W) &&
           y >= BACK_Y &&
           y < (BACK_Y + BACK_H);
}

/**
 * @brief Binds the keyboard to uiModel().serverDraft, resets case/symbol mode and sets maxLength to 64. Does
 * not copy or restore the previous address.
 */
void uiServerEditInit()
{
    keyboardState.text = &uiModel().serverDraft;
    keyboardState.capsLock = false;
    keyboardState.symbolMode = false;
    keyboardState.maxLength = 64;

    uiKeyboardInit(&keyboardState);
    keyboardReady = true;
}

/**
 * @brief Draws the complete server address editor screen using current application state.
 */
void uiServerEditDrawBase()
{
    TFT_eSPI &tft = display();

    if (!keyboardReady)
    {
        uiServerEditInit();
    }

    screenLock();
    tft.fillScreen(TFT_BLACK);
    screenUnlock();

    drawTitle();
    drawHeader();
    uiKeyboardDraw();
}

/**
 * @brief Interprets pixel coordinates on the server address editor screen and returns an action,
 * or NONE if no control matches.
 */
UiServerEditAction uiServerEditHandleTouch(uint16_t x, uint16_t y, TouchPhase phase)
{
    if (isBackTouched(x, y))
    {
        if (phase != TouchPhase::Release) return UI_SERVER_EDIT_ACTION_NONE;
        return UI_SERVER_EDIT_ACTION_BACK;
    }

    UiKeyboardAction action = uiKeyboardHandleTouch(x, y, phase);

    if (action == UI_KEYBOARD_ACTION_CHANGED)
    {
        uiModel().settingsNotice="";
        drawHeader();
        uiKeyboardDraw();
        return UI_SERVER_EDIT_ACTION_NONE;
    }

    if (action == UI_KEYBOARD_ACTION_OK)
    {
        if (phase != TouchPhase::Release) return UI_SERVER_EDIT_ACTION_NONE;
        return UI_SERVER_EDIT_ACTION_OK;
    }

    return UI_SERVER_EDIT_ACTION_NONE;
}