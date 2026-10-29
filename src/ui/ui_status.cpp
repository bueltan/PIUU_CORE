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
static uint32_t presentedCloseSequence=0;
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
    const auto closed=conversationLastClose();
    if(closed.sequence && closed.sequence!=presentedCloseSequence) {
        presentedCloseSequence=closed.sequence;
        // Do not resurrect an old notice when returning from another screen.
        if(conversationState()==ConversationState::Idle && uint32_t(millis()-closed.atMs)<5000 &&
           closed.reason!=ConversationCloseReason::UserStop &&
           closed.reason!=ConversationCloseReason::ConfigurationChanged) {
            uiStatusSetError("Conversation ended",conversationCloseReasonLabel(closed.reason));
            errorSince=closed.atMs;
        }
    }
    UiState state = UI_READY;
    if (explicitError && uint32_t(millis()-errorSince) >= 5000) explicitError=false;
    if (explicitError) state=UI_ERROR;
    else if (!appServicesWifiReady()) {
        setErrorLines("No WiFi", "Open SETUP to connect");
        state = UI_ERROR;
    } else if (!appServicesReady()) {
        setErrorLines("Audio offline", "Starting services...");
        state = UI_ERROR;
    } else if (!appServicesServerReachable()) {
        setErrorLines("No server", "Waiting backend...");
        state = UI_ERROR;
    } else if (!appServicesServerReady() && conversationState()==ConversationState::Idle) {
        setErrorLines("Server busy", "Finishing conversation...");
        state=UI_ERROR;
    } else if (conversationState() == ConversationState::Listening) state = conversationInputStandby() ? UI_STANDBY : UI_LISTENING;
    else if (conversationState() == ConversationState::Connecting) state = UI_CONNECTING;
    else if (conversationState() == ConversationState::WaitingResponse) state = UI_THINKING;
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
        case ConversationAction::Resumed:
        case ConversationAction::Stopped: explicitError=false; break;
        case ConversationAction::InputUnavailable: uiStatusSetError("EARS unavailable", "Check UART link"); break;
        case ConversationAction::NoWifi: uiStatusSetError("No WiFi", "Open SETUP to connect"); break;
        case ConversationAction::ServicesUnavailable: uiStatusSetError("Audio offline", "Starting services..."); break;
        case ConversationAction::NoServer:
            if(appServicesServerReachable()) uiStatusSetError("Server busy", "Finishing conversation...");
            else uiStatusSetError("No server", "Waiting backend...");
            break;
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
