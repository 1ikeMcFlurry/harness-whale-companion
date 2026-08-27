#pragma once

#include <stdbool.h>
#include <stdint.h>

// Called from the background network task after SNTP or a manual AT+TIME=
// command has installed a trustworthy Unix timestamp.
typedef void (*tamagotchi_time_sync_fn)(int64_t epoch_seconds, void *user);

// Reads the clock maintained by the emulated P1 ROM. This is intentionally
// separate from the ESP32 system clock so AT+TIME? can verify both clocks.
typedef bool (*tamagotchi_game_time_fn)(uint8_t *hour, uint8_t *minute,
                                        uint8_t *second, void *user);

// Starts the dedicated Tamagotchi USB console and the non-blocking Wi-Fi/SNTP
// worker. The game never waits for this function to connect.
void tamagotchi_network_start(tamagotchi_time_sync_fn sync_cb,
                              tamagotchi_game_time_fn game_time_cb,
                              void *user);
