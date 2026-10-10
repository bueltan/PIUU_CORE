#pragma once
#include <TFT_eSPI.h>
void initDisplayHardware();
TFT_eSPI& display();
// Assert ownership; retained around drawing groups for readability.
void screenLock();
void screenUnlock();
