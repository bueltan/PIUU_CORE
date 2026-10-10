#include "ui/touch_phase.h"
/**
 * @file ui_wifi.cpp
 * @brief Discovered network list, RSSI display and selection. Scanning and connection are
 * coordinated outside this view.
 */

#include "ui/screens/ui_wifi.h"

#include <TFT_eSPI.h>

#include "ui/ui_model.h"
#include "config/settings.h"
#include "hardware/display_hal.h"

static TFT_eSPI_Button btnScan;
static TFT_eSPI_Button btnBack;

static constexpr int LIST_X = 10;
static constexpr int LIST_Y = 52;
static constexpr int LIST_W = 220;
static constexpr int ROW_H = 24;
static constexpr int MAX_VISIBLE_ROWS = 7;

/**
 * @brief Draws this screen's title and associated header controls.
 */
static void drawTitle()
{
    TFT_eSPI &tft = display();

    screenLock();
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(84, 16);
    tft.print("WIFI");
    screenUnlock();
}

/**
 * @brief Redraws list status text using the module's current data.
 */
static void drawStatus()
{
    TFT_eSPI &tft = display();

    screenLock();
    tft.fillRect(10, 36, 220, 14, TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, 38);

    if (uiModel().wifiScanning)
    {
        tft.print("Scanning...");
    }
    else
    {
        tft.print("Found: ");
        tft.print(uiModel().wifi.count);
    }

    screenUnlock();
}

/**
 * @brief Initializes and draws this screen's action controls.
 */
static void drawButtons()
{
    TFT_eSPI &tft = display();

    screenLock();

    btnScan.initButton(
        &tft,
        65, 295,
        100, 34,
        TFT_GREEN, TFT_BLACK, TFT_GREEN,
        (char *)"SCAN",
        2);

    btnBack.initButton(
        &tft,
        175, 295,
        100, 34,
        TFT_GREEN, TFT_BLACK, TFT_GREEN,
        (char *)"BACK",
        2);

    btnScan.drawButton(false);
    btnBack.drawButton(false);

    screenUnlock();
}

/**
 * @brief Draws a network name and RSSI in dBm at the specified row; selected controls highlighting.
 */
static void drawNetworkRow(int row, const String &name, int rssi, bool selected)
{
    TFT_eSPI &tft = display();

    const int y = LIST_Y + row * ROW_H;
    const uint16_t bg = selected ? TFT_GREEN : TFT_BLACK;
    const uint16_t fg = selected ? TFT_BLACK : TFT_GREEN;

    screenLock();

    tft.fillRect(LIST_X, y, LIST_W, ROW_H - 2, bg);
    tft.drawRect(LIST_X, y, LIST_W, ROW_H - 2, TFT_GREEN);

    tft.setTextColor(fg, bg);
    tft.setTextSize(1);

    tft.setCursor(LIST_X + 6, y + 6);

    String shownName = name;
    if (shownName.length() > 20)
    {
        shownName = shownName.substring(0, 20);
    }
    tft.print(shownName);

    tft.setCursor(LIST_X + 180, y + 6);
    tft.print(rssi);

    screenUnlock();
}

/**
 * @brief Screen initialization hook; currently performs no operations.
 */
void uiWifiInit()
{
}

/**
 * @brief Redraws entries on the WiFi network list screen, including the current selection
 * highlight.
 */
void uiWifiRedrawList()
{
    TFT_eSPI &tft = display();

    screenLock();
    tft.fillRect(LIST_X, LIST_Y, LIST_W, ROW_H * MAX_VISIBLE_ROWS, TFT_BLACK);
    screenUnlock();

    int rows = uiModel().wifi.count;
    if (rows > MAX_VISIBLE_ROWS)
        rows = MAX_VISIBLE_ROWS;

    for (int i = 0; i < rows; i++)
    {
        drawNetworkRow(i, uiModel().wifi.entries[i].ssid, uiModel().wifi.entries[i].rssi, i == uiModel().selectedWifi);
    }
}

/**
 * @brief Draws the complete WiFi network list screen using current application state.
 */
void uiWifiDrawBase()
{
    TFT_eSPI &tft = display();

    screenLock();
    tft.fillScreen(TFT_BLACK);
    screenUnlock();

    drawTitle();
    drawStatus();
    uiWifiRedrawList();
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
    if (row < 0 || row >= uiModel().wifi.count || row >= MAX_VISIBLE_ROWS)
        return -1;

    return row;
}

/**
 * @brief Interprets pixel coordinates on the WiFi network list screen and returns an action, or
 * NONE if no control matches.
 * @param[out] selectedIndex Selected row for a SELECT action; otherwise -1.
 */
UiWifiAction uiWifiHandleTouch(uint16_t x, uint16_t y, int &selectedIndex, TouchPhase phase)
{
    selectedIndex = -1;

    if (btnScan.contains(x, y))
    {
        screenLock();
        btnScan.drawButton(phase == TouchPhase::Press);
        screenUnlock();

        if (phase != TouchPhase::Release) return UI_WIFI_ACTION_NONE;
        return UI_WIFI_ACTION_SCAN;
    }

    if (btnBack.contains(x, y))
    {
        screenLock();
        btnBack.drawButton(phase == TouchPhase::Press);
        screenUnlock();

        if (phase != TouchPhase::Release) return UI_WIFI_ACTION_NONE;
        return UI_WIFI_ACTION_BACK;
    }

    int row = getTouchedRow(x, y);
    if (row >= 0)
    {
        if (phase != TouchPhase::Release) return UI_WIFI_ACTION_NONE;
        selectedIndex = row;
        uiModel().selectedWifi = row;
        uiWifiRedrawList();
        if (phase != TouchPhase::Release) return UI_WIFI_ACTION_NONE;
        return UI_WIFI_ACTION_SELECT_NETWORK;
    }

    return UI_WIFI_ACTION_NONE;
}