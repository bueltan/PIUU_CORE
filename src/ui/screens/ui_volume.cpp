#include "ui/screens/ui_volume.h"
#include "ui/ui_model.h"
#include "hardware/display_hal.h"
#include <TFT_eSPI.h>
namespace {
TFT_eSPI_Button down, up, mute, back;
}
void uiVolumeDrawBase() {
    TFT_eSPI &tft = display();
    screenLock();
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(124, 12); tft.print("VOLUME");
    const String value = String(int(uiModel().volumePercent)) + "%";
    tft.setTextSize(3);
    tft.setCursor((320 - tft.textWidth(value)) / 2, 52); tft.print(value);
    tft.drawRect(30, 88, 260, 14, TFT_GREEN);
    const int fill = 256 * uiModel().volumePercent / 100;
    if (fill) tft.fillRect(32, 90, fill, 10, TFT_GREEN);
    tft.setTextSize(1);
    tft.setCursor(52, 113); tft.print("Live preview - saved when leaving");
    down.initButton(&tft, 65, 152, 90, 46, TFT_GREEN, TFT_BLACK, TFT_GREEN, (char*)"-", 3);
    mute.initButton(&tft, 160, 152, 90, 46, TFT_GREEN, TFT_BLACK, TFT_GREEN,
        (char*)(uiModel().volumePercent ? "MUTE" : "UNMUTE"), 2);
    up.initButton(&tft, 255, 152, 90, 46, TFT_GREEN, TFT_BLACK, TFT_GREEN, (char*)"+", 3);
    back.initButton(&tft, 160, 210, 140, 40, TFT_GREEN, TFT_BLACK, TFT_GREEN, (char*)"BACK", 2);
    down.drawButton(false); mute.drawButton(false); up.drawButton(false); back.drawButton(false);
    if (uiModel().settingsNotice.length()) {
        tft.setTextColor(TFT_ORANGE, TFT_BLACK);
        tft.setCursor(124, 180); tft.print(uiModel().settingsNotice);
    }
    screenUnlock();
}
UiVolumeAction uiVolumeHandleTouch(uint16_t x, uint16_t y, TouchPhase phase) {
    TFT_eSPI_Button *buttons[] = {&down, &up, &mute, &back};
    const UiVolumeAction actions[] = {UI_VOLUME_DOWN, UI_VOLUME_UP, UI_VOLUME_MUTE, UI_VOLUME_BACK};
    for (unsigned i=0; i<4; ++i) if (buttons[i]->contains(x,y)) {
        screenLock(); buttons[i]->drawButton(phase == TouchPhase::Press); screenUnlock();
        return phase == TouchPhase::Release ? actions[i] : UI_VOLUME_NONE;
    }
    return UI_VOLUME_NONE;
}
