/**
 * @file app_navigation.h
 * @brief Screen transitions: selects uiModel().screen and prepares or redraws the corresponding
 * view and editor.
 */

#pragma once

/**
 * @brief Activates and draws the home screen, preparing its editor when applicable.
 */
void showStartScreen();
/**
 * @brief Activates and draws the settings screen, preparing its editor when applicable.
 */
void showSetupScreen();
/**
 * @brief Activates and draws the WiFi network list screen, preparing its editor when applicable.
 */
void showWifiScreen();
/**
 * @brief Activates and draws the WiFi password screen, preparing its editor when applicable.
 */
void showWifiPasswordScreen();
/**
 * @brief Activates and draws the server list screen, preparing its editor when applicable.
 */
void showServerScreen();
/**
 * @brief Activates and draws the server address editor screen, preparing its editor when
 * applicable.
 */
void showServerEditScreen();
/**
 * @brief Shows a fatal error if the home screen is active and blocks the calling task indefinitely.
 */
void haltWithUiError();
void showEyesScreen();
