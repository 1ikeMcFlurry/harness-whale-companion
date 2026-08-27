#include "nofrendo_fast/nofrendo_fast.h"

// The 1998 core has its own C bool type.  Its public adapter uses stdbool,
// so remove the macros before including the legacy headers.
#undef bool
#undef true
#undef false

#include <bitmap.h>
#include <event.h>
#include <gui.h>
#include <log.h>
#include <nes.h>
#include <nes_rom.h>
#include <nesinput.h>
#include <nofrendo.h>
#include <osd.h>
#include <vid_drv.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct nofrendo_fast {
    nes_t *machine;
    const uint8_t *ines;
    size_t ines_size;
    uint8_t input;
    nofrendo_fast_scanline_callback_t scanline_callback;
    void *scanline_user;
};

static nofrendo_fast_t *active_core;
static nofrendo_fast_color_t palette[256];
static uint8_t dummy_surface;
static bitmap_t *dummy_bitmap;

static int video_init(int width, int height)
{
    return (width == (int)NOFRENDO_FAST_WIDTH &&
            height == (int)NOFRENDO_FAST_HEIGHT) ? 0 : -1;
}

static void video_shutdown(void)
{
}

static int video_set_mode(int width, int height)
{
    return video_init(width, height);
}

static void video_set_palette(rgb_t *source)
{
    for (unsigned i = 0; i < 256; ++i) {
        palette[i].r = (uint8_t)source[i].r;
        palette[i].g = (uint8_t)source[i].g;
        palette[i].b = (uint8_t)source[i].b;
    }
}

static void video_clear(uint8 color)
{
    (void)color;
}

static bitmap_t *video_lock(void)
{
    dummy_bitmap = bmp_createhw(&dummy_surface, NOFRENDO_FAST_WIDTH,
                                NOFRENDO_FAST_HEIGHT, NOFRENDO_FAST_WIDTH);
    return dummy_bitmap;
}

static void video_unlock(int num_dirties, rect_t *dirty_rects)
{
    (void)num_dirties;
    (void)dirty_rects;
    bmp_destroy(&dummy_bitmap);
}

static void video_blit(bitmap_t *primary, int num_dirties, rect_t *dirty_rects)
{
    (void)primary;
    (void)num_dirties;
    (void)dirty_rects;
}

static viddriver_t embedded_video_driver = {
    .name = "ESP32 embedded framebuffer",
    .init = video_init,
    .shutdown = video_shutdown,
    .set_mode = video_set_mode,
    .set_palette = video_set_palette,
    .clear = video_clear,
    .lock_write = video_lock,
    .free_write = video_unlock,
    .custom_blit = video_blit,
    .invalidate = false,
};

static bool valid_ines(const uint8_t *rom, size_t size)
{
    if (!rom || size < 16 || memcmp(rom, "NES\x1a", 4) != 0 || rom[4] == 0) {
        return false;
    }
    const size_t trainer = (rom[6] & 0x04u) ? 512u : 0u;
    const size_t needed = 16u + trainer + (size_t)rom[4] * 16384u +
                          (size_t)rom[5] * 8192u;
    return needed <= size;
}

nofrendo_fast_t *nofrendo_fast_create(const void *ines, size_t ines_size)
{
    if (active_core || !valid_ines(ines, ines_size)) {
        return NULL;
    }
    nofrendo_fast_t *core = calloc(1, sizeof(*core));
    if (!core) {
        return NULL;
    }
    core->ines = ines;
    core->ines_size = ines_size;
    active_core = core;

    log_init();
    event_init();
    event_set_system(system_nes);
    gui_init();
    if (vid_init(NOFRENDO_FAST_WIDTH, NOFRENDO_FAST_HEIGHT,
                 &embedded_video_driver) != 0 ||
        vid_setmode(NOFRENDO_FAST_WIDTH, NOFRENDO_FAST_HEIGHT) != 0) {
        nofrendo_fast_destroy(core);
        return NULL;
    }
    core->machine = nes_create();
    if (!core->machine || nes_insertcart("embedded.nes", core->machine) != 0) {
        nofrendo_fast_destroy(core);
        return NULL;
    }
    return core;
}

void nofrendo_fast_destroy(nofrendo_fast_t *core)
{
    if (!core) {
        return;
    }
    if (core->machine) {
        nes_destroy(&core->machine);
    }
    vid_shutdown();
    gui_shutdown();
    log_shutdown();
    if (active_core == core) {
        active_core = NULL;
    }
    free(core);
}

static void send_input_event(int event_code, int state)
{
    event_t handler = event_get(event_code);
    if (handler) {
        handler(state);
    }
}

_Bool nofrendo_fast_run_frame_streamed(
    nofrendo_fast_t *core, uint8_t input,
    nofrendo_fast_scanline_callback_t callback, void *user)
{
    static const int events[8] = {
        event_joypad1_a, event_joypad1_b, event_joypad1_select,
        event_joypad1_start, event_joypad1_up, event_joypad1_down,
        event_joypad1_left, event_joypad1_right,
    };
    if (!core || core != active_core || !core->machine) {
        return false;
    }
    const uint8_t changed = core->input ^ input;
    for (unsigned bit = 0; bit < 8; ++bit) {
        if (changed & (1u << bit)) {
            send_input_event(events[bit],
                             (input & (1u << bit)) ? INP_STATE_MAKE
                                                   : INP_STATE_BREAK);
        }
    }
    core->input = input;
    core->scanline_callback = callback;
    core->scanline_user = user;
    nes_renderframe(true);
    core->scanline_callback = NULL;
    core->scanline_user = NULL;
    return true;
}

_Bool nofrendo_fast_run_frame(nofrendo_fast_t *core, uint8_t input)
{
    return nofrendo_fast_run_frame_streamed(core, input, NULL, NULL);
}

void nofrendo_fast_scanlines_ready(unsigned first_line, unsigned line_count)
{
    if (active_core && active_core->scanline_callback) {
        active_core->scanline_callback(active_core->scanline_user,
                                       first_line, line_count);
    }
}

const uint8_t *nofrendo_fast_framebuffer(const nofrendo_fast_t *core,
                                         size_t *pitch)
{
    if (!core || core != active_core) {
        return NULL;
    }
    bitmap_t *bitmap = vid_getbuffer();
    if (!bitmap) {
        return NULL;
    }
    if (pitch) {
        *pitch = (size_t)bitmap->pitch;
    }
    return bitmap->data;
}

nofrendo_fast_color_t nofrendo_fast_palette_color(unsigned index)
{
    return palette[index & 0xffu];
}

size_t nofrendo_fast_save_ram_size(const nofrendo_fast_t *core)
{
    if (!core || !core->machine || !core->machine->rominfo ||
        !core->machine->rominfo->sram) {
        return 0;
    }
    return (size_t)core->machine->rominfo->sram_banks * 1024u;
}

_Bool nofrendo_fast_copy_save_ram(const nofrendo_fast_t *core,
                                  void *destination, size_t size)
{
    const size_t required = nofrendo_fast_save_ram_size(core);
    if (!required || !destination || size != required) {
        return false;
    }
    memcpy(destination, core->machine->rominfo->sram, required);
    return true;
}

_Bool nofrendo_fast_load_save_ram(nofrendo_fast_t *core,
                                  const void *source, size_t size)
{
    const size_t required = nofrendo_fast_save_ram_size(core);
    if (!required || !source || size != required) {
        return false;
    }
    memcpy(core->machine->rominfo->sram, source, required);
    return true;
}

char *osd_getromdata(void)
{
    return active_core ? (char *)active_core->ines : NULL;
}

void osd_setsound(void (*playfunc)(void *buffer, int size))
{
    (void)playfunc;
}

void osd_getvideoinfo(vidinfo_t *info)
{
    info->default_width = NOFRENDO_FAST_WIDTH;
    info->default_height = NOFRENDO_FAST_HEIGHT;
    info->driver = &embedded_video_driver;
}

void osd_getsoundinfo(sndinfo_t *info)
{
    info->sample_rate = 11025;
    info->bps = 16;
}

int osd_init(void) { return 0; }
void osd_shutdown(void) {}
int osd_main(int argc, char *argv[]) { (void)argc; (void)argv; return -1; }
int osd_installtimer(int frequency, void *func, int funcsize,
                     void *counter, int countersize)
{
    (void)frequency; (void)func; (void)funcsize; (void)counter; (void)countersize;
    return -1;
}
void osd_fullname(char *fullname, const char *shortname)
{
    snprintf(fullname, PATH_MAX + 1, "%s", shortname ? shortname : "embedded.nes");
}
char *osd_newextension(char *string, char *ext) { (void)ext; return string; }
void osd_getinput(void) {}
void osd_getmouse(int *x, int *y, int *button)
{
    if (x) *x = 0;
    if (y) *y = 0;
    if (button) *button = 0;
}
int osd_makesnapname(char *filename, int len)
{
    (void)filename; (void)len; return -1;
}
