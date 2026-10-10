#pragma once
// All lifecycle calls belong to setup()/loop(), never to a network/UI worker.
void appServicesInit();
void appServicesRefreshConnectivity();
void appServicesPoll();
bool appServicesStartIfNeeded();
bool appServicesReady();

bool appServicesWifiReady();
bool appServicesServerReady();
