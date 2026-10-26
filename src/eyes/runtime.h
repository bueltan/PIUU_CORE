#pragma once
// All methods belong to setup/loop. RX callbacks only enqueue owned commands.
void eyesInit();
void eyesPoll();
void eyesShow();
void eyesHide();
void eyesLogDiagnostics();
