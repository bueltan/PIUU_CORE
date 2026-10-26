/**
 * @file app_navigation.cpp
 * @brief Screen transitions: selects uiModel().screen and prepares or redraws the corresponding
 * view and editor.
 */

#include <Arduino.h>

#include "ui/app_navigation.h"
#include "eyes/runtime.h"

#include "ui/ui_model.h"
#include "config/settings.h"
#include "ui/ui_status.h"

#include "ui/screens/ui_start.h"
#include "ui/screens/ui_setup.h"
#include "ui/screens/ui_wifi.h"
#include "ui/screens/ui_wifi_password.h"
#include "ui/screens/ui_server.h"
#include "ui/screens/ui_server_edit.h"

/**
 * @brief Activates and draws the home screen, preparing its editor when applicable.
 */
void showEyesScreen() {
    ++uiModel().screenRevision;
    uiModel().screenEnteredMs=millis();
    uiModel().screen=SCREEN_EYES;
    eyesShow();
}

void showStartScreen()
{
    if(uiModel().screen==SCREEN_EYES)eyesHide();
    uiModel().settingsNotice="";
    ++uiModel().screenRevision;
    uiModel().screenEnteredMs=millis();
    uiModel().screen = SCREEN_START;
    uiStartDrawBase();
    uiStatusInvalidate();
    uiStatusRefreshStartScreen();
}

/**
 * @brief Activates and draws the settings screen, preparing its editor when applicable.
 */
void showSetupScreen()
{
    uiModel().settingsNotice="";
    ++uiModel().screenRevision;
    uiModel().screenEnteredMs=millis();
    uiModel().screen = SCREEN_SETUP;
    uiSetupDrawBase();
}

/**
 * @brief Activates and draws the WiFi network list screen, preparing its editor when applicable.
 */
void showWifiScreen()
{
    uiModel().settingsNotice="";
    ++uiModel().screenRevision;
    uiModel().screenEnteredMs=millis();
    uiModel().screen = SCREEN_WIFI;
    uiWifiDrawBase();
}

/**
 * @brief Activates and draws the WiFi password screen, preparing its editor when applicable.
 */
void showWifiPasswordScreen()
{
    uiModel().settingsNotice="";
    ++uiModel().screenRevision;
    uiModel().screenEnteredMs=millis();
    uiModel().screen = SCREEN_WIFI_PASSWORD;
    uiWifiPasswordInit();
    uiWifiPasswordDrawBase();
}

/**
 * @brief Activates and draws the server list screen, preparing its editor when applicable.
 */
void showServerScreen()
{
    uiModel().settingsNotice="";
    ++uiModel().screenRevision;
    uiModel().screenEnteredMs=millis();
    uiModel().screen = SCREEN_SERVER;
    uiServerDrawBase();
}

/**
 * @brief Activates and draws the server address editor screen, preparing its editor when
 * applicable.
 */
void showServerEditScreen()
{
    uiModel().settingsNotice="";
    ++uiModel().screenRevision;
    uiModel().screenEnteredMs=millis();
    uiModel().screen = SCREEN_SERVER_EDIT;
    uiServerEditInit();
    uiServerEditDrawBase();
}

/**
 * @brief Shows a fatal error if the home screen is active and blocks the calling task indefinitely.
 */
void haltWithUiError()
{
    if(uiModel().screen==SCREEN_EYES)showStartScreen();
    if (uiModel().screen == SCREEN_START)
    {
        uiStatusSetError("Fatal error", "Check serial log");
        uiStartApplyState(UI_ERROR);
    }

    while (true)
    {
        delay(1000);
    }
}