#include "nes/nes_core.h"

#include <stdlib.h>
#include <string.h>

#include "agnes.h"

struct nes_core {
    agnes_t *agnes;
    nes_core_scanline_cb_t scanline_callback;
    void *scanline_user;
};

static void on_agnes_scanline(void *user, int y, const uint8_t *color_indices,
                              size_t width) {
    nes_core_t *core = user;
    if (!core || !core->scanline_callback || y < 0 || y >= NES_CORE_SCREEN_HEIGHT) {
        return;
    }
    core->scanline_callback(core->scanline_user, (uint16_t)y, color_indices, width);
}

nes_core_t *nes_core_create(void) {
    nes_core_t *core = calloc(1, sizeof(*core));
    if (!core) {
        return NULL;
    }
    core->agnes = agnes_make();
    if (!core->agnes) {
        free(core);
        return NULL;
    }
    return core;
}

void nes_core_destroy(nes_core_t *core) {
    if (!core) {
        return;
    }
    agnes_destroy(core->agnes);
    free(core);
}

bool nes_core_load_ines(nes_core_t *core, const void *rom, size_t rom_size) {
    if (!core || !core->agnes || !rom) {
        return false;
    }
    bool loaded = agnes_load_ines_data(core->agnes, (void *)rom, rom_size);
    if (loaded) {
        agnes_set_scanline_callback(core->agnes, on_agnes_scanline, core);
    }
    return loaded;
}

void nes_core_set_scanline_callback(nes_core_t *core, nes_core_scanline_cb_t callback,
                                    void *user) {
    if (!core) {
        return;
    }
    core->scanline_callback = callback;
    core->scanline_user = user;
}

void nes_core_set_input(nes_core_t *core, const nes_core_input_t *player_one) {
    if (!core || !core->agnes || !player_one) {
        return;
    }
    const agnes_input_t input = {
        .a = player_one->a,
        .b = player_one->b,
        .select = player_one->select,
        .start = player_one->start,
        .up = player_one->up,
        .down = player_one->down,
        .left = player_one->left,
        .right = player_one->right,
    };
    agnes_set_input(core->agnes, &input, NULL);
}

bool nes_core_run_frame(nes_core_t *core) {
    return core && core->agnes && agnes_next_frame(core->agnes);
}

unsigned nes_core_mapper(const nes_core_t *core) {
    return (core && core->agnes) ? agnes_mapper(core->agnes) : 0xffu;
}

size_t nes_core_instance_size(void) {
    return sizeof(nes_core_t) + agnes_instance_size();
}

size_t nes_core_save_ram_size(const nes_core_t *core) {
    return (core && core->agnes) ? agnes_battery_ram_size(core->agnes) : 0u;
}

bool nes_core_load_save_ram(nes_core_t *core, const void *data, size_t data_size) {
    if (!core || !core->agnes || !data) {
        return false;
    }
    uint8_t *save_ram = agnes_battery_ram(core->agnes);
    const size_t save_size = agnes_battery_ram_size(core->agnes);
    if (!save_ram || data_size != save_size) {
        return false;
    }
    memcpy(save_ram, data, save_size);
    return true;
}

bool nes_core_copy_save_ram(const nes_core_t *core, void *data, size_t data_size) {
    if (!core || !core->agnes || !data) {
        return false;
    }
    uint8_t *save_ram = agnes_battery_ram(core->agnes);
    const size_t save_size = agnes_battery_ram_size(core->agnes);
    if (!save_ram || data_size != save_size) {
        return false;
    }
    memcpy(data, save_ram, save_size);
    return true;
}

nes_core_color_t nes_core_color(uint8_t color_index) {
    const agnes_color_t color = agnes_color_from_index(color_index);
    return (nes_core_color_t){.r = color.r, .g = color.g, .b = color.b};
}
