#include "ui/ui_status.h"
#include "ui/screens/ui_start.h"
#include "ui/ui_model.h"
#include "config/settings.h"

#include "app/app_services.h"
#include "app/conversation.h"

static bool dirty = true;
static UiState presentedState = UI_READY;

void uiStatusInvalidate() { dirty = true; }
static uint32_t errorSince=0;
static bool explicitError=false;
static void setErrorLines(const char *line1, const char *line2)
{
    const char *first = line1 ? line1 : "Error";
    const char *second = line2 ? line2 : "";
    if (uiModel().errorLine1 == first && uiModel().errorLine2 == second) return;
    uiModel().errorLine1 = first;
    uiModel().errorLine2 = second;
    dirty = true;
}

void uiStatusSetError(const char *line1, const char *line2) {
    setErrorLines(line1,line2);
    explicitError=true;errorSince=millis();
}

void uiStatusRefreshStartScreen()
{
    if (uiModel().screen != SCREEN_START) return;
    UiState state = UI_READY;
    if (explicitError && uint32_t(millis()-errorSince) >= 5000) explicitError=false;
    if (explicitError) state=UI_ERROR;
    else if (!appServicesWifiReady()) {
        setErrorLines("No WiFi", "Open SETUP to connect");
        state = UI_ERROR;
    } else if (!appServicesReady()) {
        setErrorLines("Audio offline", "Starting services...");
        state = UI_ERROR;
    } else if (!appServicesServerReady()) {
        setErrorLines("No server", "Waiting backend...");
        state = UI_ERROR;
    } else if (conversationState() == ConversationState::Listening) state = UI_LISTENING;
    else if ((conversationState() == ConversationState::ClosingInput || conversationState() == ConversationState::WaitingResponse)) state = UI_THINKING;
    else if (conversationState() == ConversationState::Playing) state = UI_SPEAKING;

    if (!dirty && state == presentedState) return;
    uiStartApplyState(state);
    presentedState = state;
    dirty = false;
}

void uiStatusHandleConversationToggle()
{
    switch (conversationToggleInput()) {
        case ConversationAction::Started:
        case ConversationAction::Stopped: explicitError=false; break;
        case ConversationAction::InputUnavailable: uiStatusSetError("EARS unavailable", "Check UART link"); break;
        case ConversationAction::NoWifi: uiStatusSetError("No WiFi", "Open SETUP to connect"); break;
        case ConversationAction::ServicesUnavailable: uiStatusSetError("Audio offline", "Starting services..."); break;
        case ConversationAction::NoServer: uiStatusSetError("No server", "Waiting backend..."); break;
        default: break;
    }
    uiStatusInvalidate();
    uiStatusRefreshStartScreen();
}
void uiStatusRefreshConnectivity()
{
    if (uiModel().screen != SCREEN_START) return;
    uiStartDrawWifiStatus();
    uiStatusRefreshStartScreen();
}

void uiStatusHandleEarsNotice(EarsSessionNotice notice) {
    switch (notice) {
        case EarsSessionNotice::Started: explicitError=false;uiStatusInvalidate();break;
        case EarsSessionNotice::Offline: uiStatusSetError("EARS offline","Check UART link");break;
        case EarsSessionNotice::Restarted: uiStatusSetError("EARS restarted","Start a new turn");break;
        case EarsSessionNotice::ControlLost: uiStatusSetError("EARS control lost","Start a new turn");break;
        case EarsSessionNotice::Rejected: uiStatusInvalidate();break;
        default: break;
    }
}
