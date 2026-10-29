#pragma once
struct MockIp { unsigned operator[](unsigned i) const { const unsigned ip[]={192,168,1,85};return ip[i]; } };
struct Wifi { MockIp localIP() { return {}; } int status() { return 6; } };
static Wifi WiFi;
