#pragma once
#include <stdint.h>

// Controller task only: shared playback state and optional NVS persistence.
// Request: operation(0=get,1=set,2=adjust,3=mute), signed value, persist(0/1).
// Reply: status(0=ok,1=invalid,2=save failed), live, restore, saved percent.
void deviceVolumeCommand(const uint8_t request[3],uint8_t reply[4]);
