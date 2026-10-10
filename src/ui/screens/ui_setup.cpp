#include "ui/touch_phase.h"
/**
 * @file ui_setup.cpp
 * @brief WiFi, server and touch settings menu. Returns actions for the application controller to
 * process.
 */

#include "ui/screens/ui_setup.h"

#include <TFT_eSPI.h>

#include "hardware/display_hal.h"

static TFT_eSPI_Button btnWifi;
static TFT_eSPI_Button btnServer;
static TFT_eSPI_Button btnTouch;
static TFT_eSPI_Button btnBack;

/**
 * @brief Draws this screen's title and associated header controls.
 */
static void drawTitle()
{
    TFT_eSPI &tft = display();

    screenLock();
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(75, 20);
    tft.print("SETUP");
    screenUnlock();
}

/**
 * @brief Initializes and draws this screen's action controls.
 */
static void drawButtons()
{
    TFT_eSPI &tft = display();

    screenLock();

    btnWifi.initButton(
        &tft,
        120, 85,
        160, 46,
        TFT_GREEN, TFT_BLACK, TFT_GREEN,
        (char *)"WIFI",
        2);

    btnServer.initButton(
        &tft,
        120, 145,
        160, 46,
        TFT_GREEN, TFT_BLACK, TFT_GREEN,
        (char *)"SERVER",
        2);

    btnTouch.initButton(
        &tft,
        120, 205,
        160, 46,
        TFT_GREEN, TFT_BLACK, TFT_GREEN,
        (char *)"TOUCH",
        2);

    btnBack.initButton(
        &tft,
        120, 275,
        120, 40,
        TFT_GREEN, TFT_BLACK, TFT_GREEN,
        (char *)"BACK",
        2);

    btnWifi.drawButton(false);
    btnServer.drawButton(false);
    btnTouch.drawButton(false);
    btnBack.drawButton(false);

    screenUnlock();
}

/**
 * @brief Screen initialization hook; currently performs no operations.
 */
void uiSetupInit()
{
}

/**
 * @brief Draws the complete settings screen using current application state.
 */
void uiSetupDrawBase()
{
    TFT_eSPI &tft = display();

    screenLock();
    tft.fillScreen(TFT_BLACK);
    screenUnlock();

    drawTitle();
    drawButtons();
}

/**
 * @brief Interprets pixel coordinates on the settings screen and returns an action, or NONE if
 * no control matches.
 */
UiSetupAction uiSetupHandleTouch(uint16_t x, uint16_t y, TouchPhase phase)
{
    if (btnWifi.contains(x, y))
    {
        btnWifi.drawButton(phase == TouchPhase::Press);
        if (phase != TouchPhase::Release) return UI_SETUP_ACTION_NONE;
        return UI_SETUP_ACTION_WIFI;
    }

    if (btnServer.contains(x, y))
    {
        btnServer.drawButton(phase == TouchPhase::Press);
        if (phase != TouchPhase::Release) return UI_SETUP_ACTION_NONE;
        return UI_SETUP_ACTION_SERVER;
    }

    if (btnTouch.contains(x, y))
    {
        btnTouch.drawButton(phase == TouchPhase::Press);
        if (phase != TouchPhase::Release) return UI_SETUP_ACTION_NONE;
        return UI_SETUP_ACTION_TOUCH;
    }

    if (btnBack.contains(x, y))
    {
        btnBack.drawButton(phase == TouchPhase::Press);
        if (phase != TouchPhase::Release) return UI_SETUP_ACTION_NONE;
        return UI_SETUP_ACTION_BACK;
    }

    return UI_SETUP_ACTION_NONE;
}