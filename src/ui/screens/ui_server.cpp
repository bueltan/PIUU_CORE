#include "ui/touch_phase.h"
/**
 * @file ui_server.cpp
 * @brief Saved server list and select, add, edit and delete controls. Returns actions to the
 * application controller.
 */

#include "ui/screens/ui_server.h"

#include <TFT_eSPI.h>

#include "ui/ui_model.h"
#include "config/settings.h"
#include "hardware/display_hal.h"

static TFT_eSPI_Button btnAdd;
static TFT_eSPI_Button btnEdit;
static TFT_eSPI_Button btnOk;
static TFT_eSPI_Button btnBack;

static constexpr int LIST_X = 10;
static constexpr int LIST_Y = 52;
static constexpr int LIST_W = 300;
static constexpr int ROW_H = 20;
static constexpr int MAX_VISIBLE_ROWS = 7;

static constexpr int BACK_X = 8;
static constexpr int BACK_Y = 8;
static constexpr int BACK_W = 28;
static constexpr int BACK_H = 28;

static TFT_eSPI_Button btnDelete;

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
 * @brief Draws the back control with its normal or pressed appearance.
 */
static void drawBackButton(bool pressed = false)
{
    TFT_eSPI &tft = display();

    const uint16_t bg = pressed ? TFT_GREEN : TFT_BLACK;
    const uint16_t fg = pressed ? TFT_BLACK : TFT_GREEN;

    screenLock();
    tft.fillRoundRect(BACK_X, BACK_Y, BACK_W, BACK_H, 4, bg);
    tft.drawRoundRect(BACK_X, BACK_Y, BACK_W, BACK_H, 4, TFT_GREEN);
    tft.drawBitmap(BACK_X + 6, BACK_Y + 6, backArrow16x16, 16, 16, fg);
    screenUnlock();
}
/**
 * @brief Draws this screen's title and associated header controls.
 */
static void drawTitle()
{
    TFT_eSPI &tft = display();

    screenLock();
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(124, 16);
    tft.print("SERVER");
    screenUnlock();
}

/**
 * @brief Redraws list status text using the module's current data.
 */
static void drawStatus()
{
    TFT_eSPI &tft = display();

    screenLock();
    tft.fillRect(10, 36, 300, 14, TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, 38);
    if (uiModel().settingsNotice.length()) tft.print(uiModel().settingsNotice);
    else { tft.print("Saved: "); tft.print(settings().serverCount); }
    screenUnlock();
}

/**
 * @brief Initializes and draws this screen's action controls.
 */
static void drawButtons()
{
    TFT_eSPI &tft = display();

    screenLock();

    btnAdd.initButton(&tft, 45, 216, 64, 34, TFT_GREEN, TFT_BLACK, TFT_GREEN, (char *)"ADD", 2);
    btnEdit.initButton(&tft, 121, 216, 68, 34, TFT_GREEN, TFT_BLACK, TFT_GREEN, (char *)"EDIT", 2);
    btnDelete.initButton(&tft, 199, 216, 68, 34, TFT_GREEN, TFT_BLACK, TFT_GREEN, (char *)"DEL", 2);
    btnOk.initButton(&tft, 275, 216, 64, 34, TFT_GREEN, TFT_BLACK, TFT_GREEN, (char *)"OK", 2);

    btnAdd.drawButton(false);
    btnEdit.drawButton(false);
    btnDelete.drawButton(false);
    btnOk.drawButton(false);

    screenUnlock();
}
/**
 * @brief Draws an address in the specified row and highlights it when selected is true.
 */
static void drawServerRow(int row, const String &value, bool selected)
{
    TFT_eSPI &tft = display();

    const int y = LIST_Y + row * ROW_H;
    const uint16_t bg = selected ? TFT_GREEN : TFT_BLACK;
    const uint16_t fg = selected ? TFT_BLACK : TFT_GREEN;

    String shown = value;
    if (shown.length() > 46)
    {
        shown = shown.substring(0, 46);
    }

    screenLock();

    tft.fillRect(LIST_X, y, LIST_W, ROW_H - 2, bg);
    tft.drawRect(LIST_X, y, LIST_W, ROW_H - 2, TFT_GREEN);

    tft.setTextColor(fg, bg);
    tft.setTextSize(1);
    tft.setCursor(LIST_X + 6, y + 6);
    tft.print(shown);

    screenUnlock();
}

/**
 * @brief Screen initialization hook; currently performs no operations.
 */
void uiServerInit()
{
}

/**
 * @brief Redraws entries on the server list screen, including the current selection highlight.
 */
void uiServerRedrawList()
{
    TFT_eSPI &tft = display();

    screenLock();
    tft.fillRect(LIST_X, LIST_Y, LIST_W, ROW_H * MAX_VISIBLE_ROWS, TFT_BLACK);
    screenUnlock();

    int rows = settings().serverCount;
    if (rows > MAX_VISIBLE_ROWS)
        rows = MAX_VISIBLE_ROWS;

    for (int i = 0; i < rows; i++)
    {
        drawServerRow(i, settings().servers[i], i == uiModel().selectedServer);
    }
}

/**
 * @brief Draws the complete server list screen using current application state.
 */
void uiServerDrawBase()
{
    TFT_eSPI &tft = display();

    screenLock();
    tft.fillScreen(TFT_BLACK);
    screenUnlock();

    drawTitle();
    drawBackButton(false);
    drawStatus();
    uiServerRedrawList();
    drawButtons();
}
/**
 * @brief Maps pixel coordinates to a valid list row, or returns -1 outside selectable entries.
 */
static int getTouchedRow(uint16_t x, uint16_t y)
{
    if (x < LIST_X || x >= (LIST_X + LIST_W))
        return -1;

    if (y < LIST_Y || y >= (LIST_Y + ROW_H * MAX_VISIBLE_ROWS))
        return -1;

    int row = (y - LIST_Y) / ROW_H;
    if (row < 0 || row >= settings().serverCount || row >= MAX_VISIBLE_ROWS)
        return -1;

    return row;
}

/**
 * @brief Interprets pixel coordinates on the server list screen and returns an action, or NONE
 * if no control matches.
 * @param[out] selectedIndex Selected row for a SELECT action; otherwise -1.
 */
UiServerAction uiServerHandleTouch(uint16_t x, uint16_t y, int &selectedIndex, TouchPhase phase)
{
    selectedIndex = -1;

    if (isBackTouched(x, y))
    {
        drawBackButton(phase == TouchPhase::Press);
        if (phase != TouchPhase::Release) return UI_SERVER_ACTION_NONE;
        return UI_SERVER_ACTION_BACK;
    }

    if (btnAdd.contains(x, y))
    {
        btnAdd.drawButton(phase == TouchPhase::Press);
        if (phase != TouchPhase::Release) return UI_SERVER_ACTION_NONE;
        return UI_SERVER_ACTION_ADD;
    }

    if (btnEdit.contains(x, y))
    {
        btnEdit.drawButton(phase == TouchPhase::Press);
        if (phase != TouchPhase::Release) return UI_SERVER_ACTION_NONE;
        return UI_SERVER_ACTION_EDIT;
    }

    if (btnDelete.contains(x, y))
    {
        btnDelete.drawButton(phase == TouchPhase::Press);
        if (phase != TouchPhase::Release) return UI_SERVER_ACTION_NONE;
        return UI_SERVER_ACTION_DELETE;
    }

    if (btnOk.contains(x, y))
    {
        btnOk.drawButton(phase == TouchPhase::Press);
        if (phase != TouchPhase::Release) return UI_SERVER_ACTION_NONE;
        return UI_SERVER_ACTION_OK;
    }

    int row = getTouchedRow(x, y);
    if (row >= 0)
    {
        if (phase != TouchPhase::Release) return UI_SERVER_ACTION_NONE;
        selectedIndex = row;
        uiModel().selectedServer = row;
        uiServerRedrawList();
        if (phase != TouchPhase::Release) return UI_SERVER_ACTION_NONE;
        return UI_SERVER_ACTION_SELECT;
    }

    return UI_SERVER_ACTION_NONE;
}