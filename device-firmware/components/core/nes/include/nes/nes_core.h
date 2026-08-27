#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NES_CORE_SCREEN_WIDTH = 256,
    NES_CORE_SCREEN_HEIGHT = 240,
};

typedef struct nes_core nes_core_t;

typedef struct {
    bool a;
    bool b;
    bool select;
    bool start;
    bool up;
    bool down;
    bool left;
    bool right;
} nes_core_input_t;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} nes_core_color_t;

typedef void (*nes_core_scanline_cb_t)(void *user, uint16_t y,
                                       const uint8_t *color_indices,
                                       size_t width);

nes_core_t *nes_core_create(void);
void nes_core_destroy(nes_core_t *core);

// The ROM bytes are not copied and must remain readable for the lifetime of
// the loaded game.  This makes an esp_partition_mmap() pointer a zero-copy ROM
// backend on ESP32-C3.
bool nes_core_load_ines(nes_core_t *core, const void *rom, size_t rom_size);
void nes_core_set_scanline_callback(nes_core_t *core, nes_core_scanline_cb_t callback,
                                    void *user);
void nes_core_set_input(nes_core_t *core, const nes_core_input_t *player_one);
bool nes_core_run_frame(nes_core_t *core);

unsigned nes_core_mapper(const nes_core_t *core);
size_t nes_core_instance_size(void);
size_t nes_core_save_ram_size(const nes_core_t *core);
bool nes_core_load_save_ram(nes_core_t *core, const void *data, size_t data_size);
bool nes_core_copy_save_ram(const nes_core_t *core, void *data, size_t data_size);
nes_core_color_t nes_core_color(uint8_t color_index);

#ifdef __cplusplus
}
#endif
