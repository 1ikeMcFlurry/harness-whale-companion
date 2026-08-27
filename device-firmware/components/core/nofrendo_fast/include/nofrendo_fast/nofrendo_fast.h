#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NOFRENDO_FAST_WIDTH 256u
#define NOFRENDO_FAST_HEIGHT 240u

typedef struct nofrendo_fast nofrendo_fast_t;
typedef void (*nofrendo_fast_scanline_callback_t)(void *user,
                                                   unsigned first_line,
                                                   unsigned line_count);

typedef enum {
    NOFRENDO_FAST_A      = 1u << 0,
    NOFRENDO_FAST_B      = 1u << 1,
    NOFRENDO_FAST_SELECT = 1u << 2,
    NOFRENDO_FAST_START  = 1u << 3,
    NOFRENDO_FAST_UP     = 1u << 4,
    NOFRENDO_FAST_DOWN   = 1u << 5,
    NOFRENDO_FAST_LEFT   = 1u << 6,
    NOFRENDO_FAST_RIGHT  = 1u << 7,
} nofrendo_fast_input_t;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} nofrendo_fast_color_t;

nofrendo_fast_t *nofrendo_fast_create(const void *ines, size_t ines_size);
void nofrendo_fast_destroy(nofrendo_fast_t *core);
bool nofrendo_fast_run_frame(nofrendo_fast_t *core, uint8_t input);
bool nofrendo_fast_run_frame_streamed(
    nofrendo_fast_t *core, uint8_t input,
    nofrendo_fast_scanline_callback_t callback, void *user);

const uint8_t *nofrendo_fast_framebuffer(const nofrendo_fast_t *core,
                                         size_t *pitch);
nofrendo_fast_color_t nofrendo_fast_palette_color(unsigned index);

size_t nofrendo_fast_save_ram_size(const nofrendo_fast_t *core);
bool nofrendo_fast_copy_save_ram(const nofrendo_fast_t *core,
                                 void *destination, size_t size);
bool nofrendo_fast_load_save_ram(nofrendo_fast_t *core,
                                 const void *source, size_t size);

#ifdef __cplusplus
}
#endif
