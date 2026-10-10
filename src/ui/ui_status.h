#pragma once
// Presentation only: does not start services, connect WiFi or send heartbeat.
void uiStatusSetError(const char *line1, const char *line2);
void uiStatusRefreshStartScreen();
void uiStatusRefreshConnectivity();
void uiStatusHandleConversationToggle();

// After redrawing the screen base or a transient button press.
void uiStatusInvalidate();

#include "app/ears_session.h"
void uiStatusHandleEarsNotice(EarsSessionNotice notice);
