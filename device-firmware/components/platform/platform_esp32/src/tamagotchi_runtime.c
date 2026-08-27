/*
 * ESP32-C3 adapter for TamaLIB's first-generation Tamagotchi emulator.
 *
 * TamaLIB is Copyright (C) Jean-Christophe Rona and licensed under
 * GPL-2.0-or-later. This adapter is part of the firmware that links it.
 */
#include "platform/board_config.h"

#if PERIPH_DISPLAY && PERIPH_BUTTON

#include "platform/platform_factory.h"
#include "platform/tamagotchi_network.h"

#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tamalib/tamalib.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TAMA_ROM_LABEL             "tamarom"
#define TAMA_SAVE_LABEL            "tamasave"
#define TAMA_ROM_BYTES             0x3000u
#define TAMA_PROGRAM_WORDS         (TAMA_ROM_BYTES / 2u)
#define TAMA_LCD_WIDTH             32u
#define TAMA_LCD_HEIGHT            16u
#define TAMA_ICON_COUNT            8u
#define TAMA_SCREEN_WIDTH          240u
#define TAMA_SCREEN_HEIGHT         320u
#define TAMA_RENDER_LINES          80u
#define TAMA_UI_FPS                60u
#define TAMA_LCD_X                 24
#define TAMA_LCD_Y                 96
#define TAMA_LCD_CELL              6
#define TAMA_LCD_PIXEL_WIDTH       (TAMA_LCD_WIDTH * TAMA_LCD_CELL)
#define TAMA_LCD_PIXEL_HEIGHT      (TAMA_LCD_HEIGHT * TAMA_LCD_CELL)
#define TAMA_ICON_WIDTH            46
#define TAMA_ICON_HEIGHT           25
#define TAMA_BUTTON_SIZE           39
#define TAMA_SAVE_SLOT_SIZE        0x1000u
#define TAMA_SAVE_MAGIC            0x314d4154u /* "TAM1" little-endian */
#define TAMA_SAVE_VERSION          2u
#define TAMA_AUTOSAVE_US           (30LL * 1000LL * 1000LL)
#define TAMA_AUDIO_RATE            8000u
#define TAMA_TICK_FREQUENCY        32768u
#define TAMA_MAX_CATCHUP_SECONDS   300u
#define TAMA_CATCHUP_BATCH_STEPS   256u
#define TAMA_CATCHUP_RENDER_US     250000LL
#define TAMA_CHINA_OFFSET_SECONDS  (8 * 60 * 60)
#define TAMA_CLOCK_SECOND_ONES     0x010u
#define TAMA_CLOCK_SECOND_TENS     0x011u
#define TAMA_CLOCK_MINUTE_ONES     0x012u
#define TAMA_CLOCK_MINUTE_TENS     0x013u
#define TAMA_CLOCK_HOUR_LOW        0x014u
#define TAMA_CLOCK_HOUR_HIGH       0x015u

static const char *TAG = "tamagotchi";

// Private bridge exported only inside the ESP32 platform component.
esp_lcd_panel_io_handle_t platform_display_io(hal_display_t *display);

typedef struct __attribute__((packed)) {
    uint16_t pc;
    uint16_t x;
    uint16_t y;
    uint8_t a;
    uint8_t b;
    uint8_t np;
    uint8_t sp;
    uint8_t flags;
    uint32_t timers[10]; // tick, 2..256 Hz timestamps, program timer timestamp
    uint8_t prog_timer_enabled;
    uint8_t prog_timer_data;
    uint8_t prog_timer_rld;
    uint32_t call_depth;
    uint8_t interrupt_factor[INT_SLOT_NUM];
    uint8_t interrupt_mask[INT_SLOT_NUM];
    uint8_t interrupt_triggered[INT_SLOT_NUM];
    uint8_t cpu_halted;
    uint8_t memory[MEM_BUFFER_SIZE * sizeof(MEM_BUFFER_TYPE)];
} tama_save_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t generation;
    uint32_t payload_size;
    uint32_t crc32;
    int64_t saved_epoch;
} tama_save_header_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t generation;
    uint32_t payload_size;
    uint32_t crc32;
} tama_save_header_v1_t;

typedef struct {
    hal_display_t *display;
    esp_lcd_panel_io_handle_t display_io;
    TaskHandle_t display_task;
    volatile uint32_t display_completed;
    uint32_t display_submitted;
    uint32_t display_timeouts;
    const esp_partition_t *save_partition;
    u12_t *program;
    uint16_t *render_buffer;
    uint8_t lcd[TAMA_LCD_HEIGHT][TAMA_LCD_WIDTH];
    uint8_t icons[TAMA_ICON_COUNT];
    uint8_t button_down[3];
    volatile uint32_t pending_pressed;
    volatile uint32_t pending_released;
    volatile uint32_t lcd_dirty_rows;
    volatile uint32_t icon_dirty_mask;
    volatile uint32_t button_dirty_mask;
    uint32_t ui_update_count;
    uint32_t ui_bytes;
    uint64_t ui_time_us;
    int64_t ui_metrics_started_us;
    uint32_t save_generation;
    int save_active_slot;
    int64_t last_save_us;
    int64_t next_save_us;
    int64_t last_yield_us;
    int64_t time_anchor_epoch;
    int64_t time_anchor_us;
    volatile bool time_sync_pending;
    volatile int64_t pending_sync_epoch;
    volatile bool catchup_active;
    uint64_t catchup_ticks_remaining;
    uint64_t catchup_ticks_total;
    int64_t catchup_target_epoch;
    int64_t catchup_started_us;
    int64_t last_catchup_render_us;
#if PERIPH_AUDIO
    hal_audio_t *audio;
    volatile uint32_t sound_frequency_hz;
    volatile bool sound_enabled;
#endif
} tama_runtime_t;

static tama_runtime_t *s_runtime;
static portMUX_TYPE s_time_mux = portMUX_INITIALIZER_UNLOCKED;

static inline uint16_t rgb565_be(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t value = (uint16_t)(((uint16_t)(r & 0xf8u) << 8) |
                                ((uint16_t)(g & 0xfcu) << 3) |
                                ((uint16_t)b >> 3));
    return __builtin_bswap16(value);
}

typedef struct {
    uint16_t *pixels;
    int x0;
    int y0;
    int width;
    int height;
    int stride;
} canvas_t;

static void canvas_pixel(canvas_t *canvas, int x, int y, uint16_t color) {
    if (x < canvas->x0 || x >= canvas->x0 + canvas->width ||
        y < canvas->y0 ||
        y >= canvas->y0 + canvas->height) return;
    canvas->pixels[(y - canvas->y0) * canvas->stride + (x - canvas->x0)] = color;
}

static void canvas_rect(canvas_t *canvas, int x, int y, int width, int height,
                        uint16_t color) {
    int y_start = y > canvas->y0 ? y : canvas->y0;
    int y_end = y + height < canvas->y0 + canvas->height
                    ? y + height : canvas->y0 + canvas->height;
    int x_start = x > canvas->x0 ? x : canvas->x0;
    int x_end = x + width < canvas->x0 + canvas->width
                    ? x + width : canvas->x0 + canvas->width;
    for (int row = y_start; row < y_end; ++row) {
        uint16_t *destination = canvas->pixels +
            (row - canvas->y0) * canvas->stride + (x_start - canvas->x0);
        for (int column = x_start; column < x_end; ++column) {
            *destination++ = color;
        }
    }
}

static const char FONT_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789=.-";
static const uint8_t FONT_ROWS[][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},{7,2,2,2,2,18,12},
    {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
    {0,31,0,31,0,0,0},{0,0,0,0,0,12,12},{0,0,0,31,0,0,0},
};

static const uint8_t *font_rows(char character) {
    const char *position = strchr(FONT_CHARS, character);
    return position ? FONT_ROWS[position - FONT_CHARS] : NULL;
}

static void canvas_text(canvas_t *canvas, int x, int y, const char *text,
                        int scale, uint16_t color) {
    for (const char *character = text; *character; ++character) {
        const uint8_t *rows = font_rows(*character);
        if (rows) {
            for (int row = 0; row < 7; ++row) {
                for (int column = 0; column < 5; ++column) {
                    if (rows[row] & (1u << (4 - column))) {
                        canvas_rect(canvas, x + column * scale, y + row * scale,
                                    scale, scale, color);
                    }
                }
            }
        }
        x += 6 * scale;
    }
}

static void canvas_circle(canvas_t *canvas, int center_x, int center_y, int radius,
                          uint16_t color) {
    for (int y = center_y - radius; y <= center_y + radius; ++y) {
        for (int x = center_x - radius; x <= center_x + radius; ++x) {
            int dx = x - center_x;
            int dy = y - center_y;
            if (dx * dx + dy * dy <= radius * radius) {
                canvas_pixel(canvas, x, y, color);
            }
        }
    }
}

static const int ICON_X[4] = {20, 71, 122, 173};
static const int ICON_Y[2] = {51, 212};
static const int BUTTON_X[3] = {50, 120, 190};
static const unsigned BUTTON_PHYSICAL_INDEX[3] = {0, 2, 1};

static const char *ICON_LABELS[TAMA_ICON_COUNT] = {
    "FOOD", "LITE", "PLAY", "MED",
    "BATH", "STAT", "DISC", "CALL",
};

static void draw_icon(canvas_t *canvas, tama_runtime_t *runtime, unsigned index) {
    const int x = ICON_X[index & 3u];
    const int y = ICON_Y[index >> 2u];
    const uint16_t ink = rgb565_be(37, 35, 58);
    const uint16_t pearl = rgb565_be(250, 246, 224);
    const uint16_t coral = rgb565_be(255, 103, 122);
    const uint16_t glyph = runtime->icons[index] ? pearl : ink;
    canvas_rect(canvas, x, y, TAMA_ICON_WIDTH, TAMA_ICON_HEIGHT, ink);
    canvas_rect(canvas, x + 2, y + 2, TAMA_ICON_WIDTH - 4,
                TAMA_ICON_HEIGHT - 4,
                runtime->icons[index] ? coral : pearl);
    const int text_width = (int)strlen(ICON_LABELS[index]) * 6 - 1;
    canvas_text(canvas, x + (TAMA_ICON_WIDTH - text_width) / 2,
                y + 9, ICON_LABELS[index], 1, glyph);
}

static void draw_lcd(canvas_t *canvas, tama_runtime_t *runtime,
                     unsigned first_row, unsigned row_count) {
    const uint16_t lcd_off = rgb565_be(196, 215, 158);
    const uint16_t lcd_on = rgb565_be(38, 55, 48);
    const int top = TAMA_LCD_Y + (int)first_row * TAMA_LCD_CELL;
    canvas_rect(canvas, TAMA_LCD_X, top, TAMA_LCD_PIXEL_WIDTH,
                (int)row_count * TAMA_LCD_CELL, lcd_off);
    for (unsigned y = first_row; y < first_row + row_count; ++y) {
        for (unsigned x = 0; x < TAMA_LCD_WIDTH; ++x) {
            if (runtime->lcd[y][x]) {
                canvas_rect(canvas,
                            TAMA_LCD_X + (int)x * TAMA_LCD_CELL,
                            TAMA_LCD_Y + (int)y * TAMA_LCD_CELL,
                            TAMA_LCD_CELL - 1, TAMA_LCD_CELL - 1, lcd_on);
            }
        }
    }
}

static void draw_button(canvas_t *canvas, tama_runtime_t *runtime, unsigned index) {
    const uint16_t ink = rgb565_be(37, 35, 58);
    const uint16_t pearl = rgb565_be(255, 246, 216);
    const uint16_t coral = rgb565_be(255, 103, 122);
    const uint16_t lemon = rgb565_be(255, 218, 105);
    static const char *button_name[3] = {"A", "B", "C"};
    const bool pressed = runtime->button_down[BUTTON_PHYSICAL_INDEX[index]] != 0;
    canvas_circle(canvas, BUTTON_X[index], 270, 19, pearl);
    canvas_circle(canvas, BUTTON_X[index], 270, 15, pressed ? lemon : coral);
    canvas_text(canvas, BUTTON_X[index] - 5, 263, button_name[index], 2, ink);
}

static void draw_meteor(canvas_t *canvas, int head_x, int head_y, int direction) {
    const uint16_t pearl = rgb565_be(255, 246, 216);
    const uint16_t lemon = rgb565_be(255, 218, 105);
    canvas_rect(canvas, head_x, head_y, 5, 5, lemon);
    canvas_rect(canvas, head_x + 1, head_y - 2, 3, 9, lemon);
    for (int i = 1; i <= 6; ++i) {
        const int size = i < 3 ? 3 : 2;
        canvas_rect(canvas, head_x + direction * i * 3,
                    head_y - direction * i * 3, size, size, pearl);
    }
}

static void draw_scene(canvas_t *canvas, tama_runtime_t *runtime) {
    const uint16_t sky = rgb565_be(42, 47, 90);
    const uint16_t pearl = rgb565_be(255, 246, 216);
    const uint16_t ink = rgb565_be(37, 35, 58);
    const uint16_t shadow = rgb565_be(22, 20, 37);
    const uint16_t lcd_off = rgb565_be(196, 215, 158);

    canvas_rect(canvas, 0, 0, TAMA_SCREEN_WIDTH, TAMA_SCREEN_HEIGHT, sky);

    static const uint16_t stars[][2] = {
        {12,18},{34,37},{207,43},{226,18},{9,226},{229,238},{16,307},{220,302}
    };
    for (unsigned i = 0; i < sizeof(stars) / sizeof(stars[0]); ++i) {
        canvas_rect(canvas, stars[i][0], stars[i][1], 2, 2, pearl);
    }
    draw_meteor(canvas, 18, 42, -1);
    draw_meteor(canvas, 217, 286, 1);
    canvas_text(canvas, 78, 17, "TAMA P1", 2, pearl);
    canvas_text(canvas, 90, 36, "POCKET PET", 1, pearl);

    for (unsigned i = 0; i < 4; ++i) draw_icon(canvas, runtime, i);

    canvas_rect(canvas, 18, 85, 208, 120, shadow);
    canvas_rect(canvas, 14, 81, 212, 120, ink);
    canvas_rect(canvas, 20, 87, 200, 108, lcd_off);
    draw_lcd(canvas, runtime, 0, TAMA_LCD_HEIGHT);

    for (unsigned i = 4; i < TAMA_ICON_COUNT; ++i) draw_icon(canvas, runtime, i);

    for (unsigned i = 0; i < 3; ++i) draw_button(canvas, runtime, i);
    canvas_text(canvas, 38, 296, "A=UP", 1, pearl);
    canvas_text(canvas, 108, 296, "B=OK", 1, pearl);
    canvas_text(canvas, 178, 296, "C=DN", 1, pearl);
}

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                esp_lcd_panel_io_event_data_t *event,
                                void *user) {
    (void)io;
    (void)event;
    tama_runtime_t *runtime = user;
    __atomic_add_fetch(&runtime->display_completed, 1u, __ATOMIC_RELEASE);
    BaseType_t task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(runtime->display_task, &task_woken);
    return task_woken == pdTRUE;
}

static bool wait_for_display(tama_runtime_t *runtime, uint32_t sequence) {
    const int64_t deadline = esp_timer_get_time() + 100000;
    while (__atomic_load_n(&runtime->display_completed,
                           __ATOMIC_ACQUIRE) < sequence) {
        const int64_t remaining_us = deadline - esp_timer_get_time();
        if (remaining_us <= 0) return false;
        TickType_t ticks = pdMS_TO_TICKS((uint32_t)(remaining_us + 999) / 1000u);
        if (!ticks) ticks = 1;
        if (ulTaskNotifyTake(pdTRUE, ticks) == 0) return false;
    }
    return true;
}

static void flush_region(tama_runtime_t *runtime, int x, int y,
                         int width, int height) {
    const uint32_t sequence = ++runtime->display_submitted;
    hal_display_flush(runtime->display, x, y, x + width - 1,
                      y + height - 1, runtime->render_buffer);
    if (!wait_for_display(runtime, sequence)) {
        runtime->display_timeouts++;
        ESP_LOGE(TAG,
                 "LCD DMA timeout #%u submitted=%u completed=%u region=%d,%d %dx%d",
                 (unsigned)runtime->display_timeouts,
                 (unsigned)runtime->display_submitted,
                 (unsigned)__atomic_load_n(&runtime->display_completed,
                                            __ATOMIC_ACQUIRE),
                 x, y, width, height);
    }
}

static void render_screen(tama_runtime_t *runtime) {
    for (int y = 0; y < (int)TAMA_SCREEN_HEIGHT; y += TAMA_RENDER_LINES) {
        const int height = y + TAMA_RENDER_LINES <= TAMA_SCREEN_HEIGHT
            ? TAMA_RENDER_LINES : TAMA_SCREEN_HEIGHT - y;
        canvas_t canvas = {
            .pixels = runtime->render_buffer,
            .x0 = 0,
            .y0 = y,
            .width = TAMA_SCREEN_WIDTH,
            .height = height,
            .stride = TAMA_SCREEN_WIDTH,
        };
        draw_scene(&canvas, runtime);
        flush_region(runtime, 0, y, TAMA_SCREEN_WIDTH, height);
    }
}

static unsigned first_set_row(uint32_t rows) {
    return (unsigned)__builtin_ctz(rows);
}

static unsigned last_set_row(uint32_t rows) {
    return 31u - (unsigned)__builtin_clz(rows);
}

static uint32_t render_dynamic(tama_runtime_t *runtime, uint32_t lcd_rows,
                               uint32_t icons, uint32_t buttons) {
    uint32_t bytes = 0;
    if (lcd_rows) {
        const unsigned first = first_set_row(lcd_rows);
        const unsigned last = last_set_row(lcd_rows);
        const unsigned count = last - first + 1u;
        const int height = (int)count * TAMA_LCD_CELL;
        canvas_t canvas = {
            .pixels = runtime->render_buffer,
            .x0 = TAMA_LCD_X,
            .y0 = TAMA_LCD_Y + (int)first * TAMA_LCD_CELL,
            .width = TAMA_LCD_PIXEL_WIDTH,
            .height = height,
            .stride = TAMA_LCD_PIXEL_WIDTH,
        };
        draw_lcd(&canvas, runtime, first, count);
        flush_region(runtime, canvas.x0, canvas.y0, canvas.width, canvas.height);
        bytes += (uint32_t)canvas.width * (uint32_t)canvas.height * 2u;
    }
    for (unsigned i = 0; i < TAMA_ICON_COUNT; ++i) {
        if (!(icons & (1u << i))) continue;
        canvas_t canvas = {
            .pixels = runtime->render_buffer,
            .x0 = ICON_X[i & 3u],
            .y0 = ICON_Y[i >> 2u],
            .width = TAMA_ICON_WIDTH,
            .height = TAMA_ICON_HEIGHT,
            .stride = TAMA_ICON_WIDTH,
        };
        draw_icon(&canvas, runtime, i);
        flush_region(runtime, canvas.x0, canvas.y0, canvas.width, canvas.height);
        bytes += TAMA_ICON_WIDTH * TAMA_ICON_HEIGHT * 2u;
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (!(buttons & (1u << i))) continue;
        canvas_t canvas = {
            .pixels = runtime->render_buffer,
            .x0 = BUTTON_X[i] - 19,
            .y0 = 251,
            .width = TAMA_BUTTON_SIZE,
            .height = TAMA_BUTTON_SIZE,
            .stride = TAMA_BUTTON_SIZE,
        };
        canvas_rect(&canvas, canvas.x0, canvas.y0, canvas.width, canvas.height,
                    rgb565_be(42, 47, 90));
        draw_button(&canvas, runtime, i);
        flush_region(runtime, canvas.x0, canvas.y0, canvas.width, canvas.height);
        bytes += TAMA_BUTTON_SIZE * TAMA_BUTTON_SIZE * 2u;
    }
    return bytes;
}

static void *tama_malloc(u32_t size) { return malloc(size); }
static void tama_free(void *pointer) { free(pointer); }
static void tama_halt(void) { ESP_LOGW(TAG, "Tama CPU halted"); }
static bool_t tama_is_log_enabled(log_level_t level) {
    return level == LOG_ERROR;
}

static void tama_log(log_level_t level, char *format, ...) {
    if (level != LOG_ERROR) return;
    char buffer[160];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    size_t length = strlen(buffer);
    while (length && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r')) {
        buffer[--length] = '\0';
    }
    ESP_LOGE(TAG, "%s", buffer);
}

static timestamp_t tama_timestamp(void) {
    return (timestamp_t)esp_timer_get_time();
}

static void tama_sleep_until(timestamp_t target) {
    int32_t remaining = (int32_t)(target - tama_timestamp());
    if (remaining > 1500) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)(remaining - 500) / 1000u));
        remaining = (int32_t)(target - tama_timestamp());
    }
    if (remaining > 0) esp_rom_delay_us((uint32_t)remaining);
}

static void tama_update_screen(void) {
    tama_runtime_t *runtime = s_runtime;
    if (!runtime) return;
    const int64_t now = esp_timer_get_time();
    // Fast-forward can generate thousands of LCD changes per second. Keeping
    // a 4 Hz preview proves that the device is alive without allowing SPI
    // drawing to dominate the catch-up work.
    if (runtime->catchup_active &&
        now - runtime->last_catchup_render_us < TAMA_CATCHUP_RENDER_US) {
        return;
    }
    if (runtime->catchup_active) runtime->last_catchup_render_us = now;
    const uint32_t lcd_rows = __atomic_exchange_n(&runtime->lcd_dirty_rows, 0,
                                                   __ATOMIC_ACQ_REL);
    const uint32_t icons = __atomic_exchange_n(&runtime->icon_dirty_mask, 0,
                                                __ATOMIC_ACQ_REL);
    const uint32_t buttons = __atomic_exchange_n(&runtime->button_dirty_mask, 0,
                                                  __ATOMIC_ACQ_REL);
    if (!(lcd_rows | icons | buttons)) return;
    const int64_t started = esp_timer_get_time();
    const uint32_t bytes = render_dynamic(runtime, lcd_rows, icons, buttons);
    runtime->ui_update_count++;
    runtime->ui_bytes += bytes;
    runtime->ui_time_us += (uint64_t)(esp_timer_get_time() - started);
}

static void tama_set_lcd_matrix(u8_t x, u8_t y, bool_t value) {
    tama_runtime_t *runtime = s_runtime;
    if (!runtime || x >= TAMA_LCD_WIDTH || y >= TAMA_LCD_HEIGHT) return;
    uint8_t normalized = value ? 1u : 0u;
    if (runtime->lcd[y][x] != normalized) {
        runtime->lcd[y][x] = normalized;
        __atomic_fetch_or(&runtime->lcd_dirty_rows, 1u << y, __ATOMIC_RELEASE);
    }
}

static void tama_set_lcd_icon(u8_t icon, bool_t value) {
    tama_runtime_t *runtime = s_runtime;
    if (!runtime || icon >= TAMA_ICON_COUNT) return;
    uint8_t normalized = value ? 1u : 0u;
    if (runtime->icons[icon] != normalized) {
        runtime->icons[icon] = normalized;
        __atomic_fetch_or(&runtime->icon_dirty_mask, 1u << icon,
                          __ATOMIC_RELEASE);
    }
}

#if PERIPH_AUDIO
static void tama_set_frequency(u32_t decihertz) {
    if (s_runtime) {
        __atomic_store_n(&s_runtime->sound_frequency_hz, decihertz / 10u,
                         __ATOMIC_RELEASE);
    }
}

static void tama_play_frequency(bool_t enabled) {
    if (s_runtime) {
        __atomic_store_n(&s_runtime->sound_enabled, enabled != 0,
                         __ATOMIC_RELEASE);
    }
}

static void audio_task(void *argument) {
    tama_runtime_t *runtime = argument;
    int16_t samples[128];
    uint32_t phase = 0;
    for (;;) {
        const bool enabled = __atomic_load_n(&runtime->sound_enabled, __ATOMIC_ACQUIRE);
        const uint32_t frequency = __atomic_load_n(&runtime->sound_frequency_hz,
                                                    __ATOMIC_ACQUIRE);
        if (!enabled || frequency == 0 || runtime->catchup_active) {
            memset(samples, 0, sizeof(samples));
        } else {
            uint32_t step = (uint32_t)(((uint64_t)frequency << 32) / TAMA_AUDIO_RATE);
            for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
                samples[i] = (phase & 0x80000000u) ? 3500 : -3500;
                phase += step;
            }
        }
        size_t written = 0;
        if (hal_audio_write(runtime->audio, samples, sizeof(samples), &written) != 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}
#else
static void tama_set_frequency(u32_t decihertz) { (void)decihertz; }
static void tama_play_frequency(bool_t enabled) { (void)enabled; }
#endif

static bool generation_is_newer(uint32_t lhs, uint32_t rhs) {
    return (int32_t)(lhs - rhs) > 0;
}

static int64_t runtime_state_epoch(const tama_runtime_t *runtime, int64_t now_us) {
    if (runtime->catchup_active && runtime->catchup_target_epoch > 0) {
        uint64_t seconds_left =
            (runtime->catchup_ticks_remaining + TAMA_TICK_FREQUENCY - 1u) /
            TAMA_TICK_FREQUENCY;
        return runtime->catchup_target_epoch - (int64_t)seconds_left;
    }
    if (runtime->time_anchor_epoch <= 0) return 0;
    int64_t elapsed_us = now_us - runtime->time_anchor_us;
    if (elapsed_us < 0) elapsed_us = 0;
    return runtime->time_anchor_epoch + elapsed_us / 1000000LL;
}

static void capture_state(tama_save_payload_t *payload) {
    state_t *state = tamalib_get_state();
    memset(payload, 0, sizeof(*payload));
    payload->pc = *state->pc;
    payload->x = *state->x;
    payload->y = *state->y;
    payload->a = *state->a;
    payload->b = *state->b;
    payload->np = *state->np;
    payload->sp = *state->sp;
    payload->flags = *state->flags;
    payload->timers[0] = *state->tick_counter;
    payload->timers[1] = *state->clk_timer_2hz_timestamp;
    payload->timers[2] = *state->clk_timer_4hz_timestamp;
    payload->timers[3] = *state->clk_timer_8hz_timestamp;
    payload->timers[4] = *state->clk_timer_16hz_timestamp;
    payload->timers[5] = *state->clk_timer_32hz_timestamp;
    payload->timers[6] = *state->clk_timer_64hz_timestamp;
    payload->timers[7] = *state->clk_timer_128hz_timestamp;
    payload->timers[8] = *state->clk_timer_256hz_timestamp;
    payload->timers[9] = *state->prog_timer_timestamp;
    payload->prog_timer_enabled = *state->prog_timer_enabled;
    payload->prog_timer_data = *state->prog_timer_data;
    payload->prog_timer_rld = *state->prog_timer_rld;
    payload->call_depth = *state->call_depth;
    for (unsigned i = 0; i < INT_SLOT_NUM; ++i) {
        payload->interrupt_factor[i] = state->interrupts[i].factor_flag_reg;
        payload->interrupt_mask[i] = state->interrupts[i].mask_reg;
        payload->interrupt_triggered[i] = state->interrupts[i].triggered;
    }
    payload->cpu_halted = *state->cpu_halted;
    memcpy(payload->memory, state->memory, sizeof(payload->memory));
}

static void restore_state(const tama_save_payload_t *payload) {
    state_t *state = tamalib_get_state();
    *state->pc = payload->pc;
    *state->x = payload->x;
    *state->y = payload->y;
    *state->a = payload->a;
    *state->b = payload->b;
    *state->np = payload->np;
    *state->sp = payload->sp;
    *state->flags = payload->flags;
    *state->tick_counter = payload->timers[0];
    *state->clk_timer_2hz_timestamp = payload->timers[1];
    *state->clk_timer_4hz_timestamp = payload->timers[2];
    *state->clk_timer_8hz_timestamp = payload->timers[3];
    *state->clk_timer_16hz_timestamp = payload->timers[4];
    *state->clk_timer_32hz_timestamp = payload->timers[5];
    *state->clk_timer_64hz_timestamp = payload->timers[6];
    *state->clk_timer_128hz_timestamp = payload->timers[7];
    *state->clk_timer_256hz_timestamp = payload->timers[8];
    *state->prog_timer_timestamp = payload->timers[9];
    *state->prog_timer_enabled = payload->prog_timer_enabled;
    *state->prog_timer_data = payload->prog_timer_data;
    *state->prog_timer_rld = payload->prog_timer_rld;
    *state->call_depth = payload->call_depth;
    for (unsigned i = 0; i < INT_SLOT_NUM; ++i) {
        state->interrupts[i].factor_flag_reg = payload->interrupt_factor[i];
        state->interrupts[i].mask_reg = payload->interrupt_mask[i];
        state->interrupts[i].triggered = payload->interrupt_triggered[i];
    }
    *state->cpu_halted = payload->cpu_halted;
    memcpy(state->memory, payload->memory, sizeof(payload->memory));
    tamalib_refresh_hw();
    tamalib_set_exec_mode(EXEC_MODE_RUN);
}

static bool read_save_slot(tama_runtime_t *runtime, unsigned slot,
                           tama_save_header_t *header,
                           tama_save_payload_t *payload) {
    size_t offset = (size_t)slot * TAMA_SAVE_SLOT_SIZE;
    tama_save_header_v1_t prefix;
    if (esp_partition_read(runtime->save_partition, offset, &prefix,
                           sizeof(prefix)) != ESP_OK ||
        prefix.magic != TAMA_SAVE_MAGIC ||
        (prefix.version != 1u && prefix.version != TAMA_SAVE_VERSION) ||
        prefix.payload_size != sizeof(*payload)) {
        return false;
    }
    size_t header_size;
    if (prefix.version == 1u) {
        *header = (tama_save_header_t) {
            .magic = prefix.magic,
            .version = prefix.version,
            .generation = prefix.generation,
            .payload_size = prefix.payload_size,
            .crc32 = prefix.crc32,
            .saved_epoch = 0,
        };
        header_size = sizeof(prefix);
    } else {
        if (esp_partition_read(runtime->save_partition, offset, header,
                               sizeof(*header)) != ESP_OK) return false;
        header_size = sizeof(*header);
    }
    if (header_size + sizeof(*payload) > TAMA_SAVE_SLOT_SIZE ||
        esp_partition_read(runtime->save_partition, offset + header_size,
                           payload, sizeof(*payload)) != ESP_OK) return false;
    return esp_rom_crc32_le(0, (const uint8_t *)payload,
                            sizeof(*payload)) == header->crc32;
}

static bool load_state(tama_runtime_t *runtime) {
    if (!runtime->save_partition ||
        runtime->save_partition->size < TAMA_SAVE_SLOT_SIZE) return false;
    const unsigned slot_count = runtime->save_partition->size / TAMA_SAVE_SLOT_SIZE;
    bool found = false;
    tama_save_header_t best_header = {0};
    tama_save_payload_t payload;
    tama_save_payload_t best_payload;
    for (unsigned slot = 0; slot < slot_count; ++slot) {
        tama_save_header_t header;
        if (read_save_slot(runtime, slot, &header, &payload) &&
            (!found || generation_is_newer(header.generation,
                                            best_header.generation))) {
            found = true;
            best_header = header;
            best_payload = payload;
            runtime->save_active_slot = (int)slot;
        }
    }
    if (!found) return false;
    runtime->save_generation = best_header.generation;
    runtime->time_anchor_epoch = best_header.saved_epoch;
    runtime->time_anchor_us = esp_timer_get_time();
    restore_state(&best_payload);
    ESP_LOGI(TAG, "Restored pet state generation=%u slot=%d saved_epoch=%lld",
             (unsigned)runtime->save_generation, runtime->save_active_slot,
             (long long)best_header.saved_epoch);
    return true;
}

static bool save_state(tama_runtime_t *runtime) {
    if (!runtime->save_partition) return false;
    const unsigned slot_count = runtime->save_partition->size / TAMA_SAVE_SLOT_SIZE;
    if (!slot_count) return false;
    unsigned slot = runtime->save_active_slot < 0 ? 0u :
        ((unsigned)runtime->save_active_slot + 1u) % slot_count;
    tama_save_payload_t payload;
    capture_state(&payload);
    tama_save_header_t header = {
        .magic = TAMA_SAVE_MAGIC,
        .version = TAMA_SAVE_VERSION,
        .generation = runtime->save_generation + 1u,
        .payload_size = sizeof(payload),
        .crc32 = esp_rom_crc32_le(0, (const uint8_t *)&payload, sizeof(payload)),
        .saved_epoch = runtime_state_epoch(runtime, esp_timer_get_time()),
    };
    size_t offset = (size_t)slot * TAMA_SAVE_SLOT_SIZE;
    if (esp_partition_erase_range(runtime->save_partition, offset,
                                  TAMA_SAVE_SLOT_SIZE) != ESP_OK ||
        esp_partition_write(runtime->save_partition, offset, &header,
                            sizeof(header)) != ESP_OK ||
        esp_partition_write(runtime->save_partition, offset + sizeof(header),
                            &payload, sizeof(payload)) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to save pet state in slot %u", slot);
        return false;
    }
    runtime->save_generation = header.generation;
    runtime->save_active_slot = (int)slot;
    runtime->last_save_us = esp_timer_get_time();
    runtime->next_save_us = runtime->last_save_us + TAMA_AUTOSAVE_US;
    ESP_LOGI(TAG, "Saved pet state generation=%u slot=%u epoch=%lld",
             (unsigned)header.generation, slot, (long long)header.saved_epoch);
    return true;
}

static void set_game_clock(int64_t epoch_seconds) {
    time_t china_epoch = (time_t)(epoch_seconds + TAMA_CHINA_OFFSET_SECONDS);
    struct tm china = {0};
    gmtime_r(&china_epoch, &china);

    state_t *state = tamalib_get_state();
    SET_RAM_MEMORY(state->memory, TAMA_CLOCK_SECOND_ONES, china.tm_sec % 10);
    SET_RAM_MEMORY(state->memory, TAMA_CLOCK_SECOND_TENS, china.tm_sec / 10);
    SET_RAM_MEMORY(state->memory, TAMA_CLOCK_MINUTE_ONES, china.tm_min % 10);
    SET_RAM_MEMORY(state->memory, TAMA_CLOCK_MINUTE_TENS, china.tm_min / 10);
    SET_RAM_MEMORY(state->memory, TAMA_CLOCK_HOUR_LOW, china.tm_hour & 0x0f);
    SET_RAM_MEMORY(state->memory, TAMA_CLOCK_HOUR_HIGH,
                   (china.tm_hour >> 4) & 0x0f);
    ESP_LOGI(TAG, "Game clock synchronized to %02d:%02d:%02d UTC+8",
             china.tm_hour, china.tm_min, china.tm_sec);
}

static bool read_game_clock(uint8_t *hour, uint8_t *minute,
                            uint8_t *second, void *user) {
    (void)user;
    if (!hour || !minute || !second) return false;
    state_t *state = tamalib_get_state();
    if (!state || !state->memory) return false;

    // The console task may read while the emulator advances the seconds. Read
    // twice and retry once if the second changed across the snapshot.
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        const uint8_t second_before =
            GET_RAM_MEMORY(state->memory, TAMA_CLOCK_SECOND_ONES) +
            10u * GET_RAM_MEMORY(state->memory, TAMA_CLOCK_SECOND_TENS);
        const uint8_t sampled_minute =
            GET_RAM_MEMORY(state->memory, TAMA_CLOCK_MINUTE_ONES) +
            10u * GET_RAM_MEMORY(state->memory, TAMA_CLOCK_MINUTE_TENS);
        const uint8_t sampled_hour =
            GET_RAM_MEMORY(state->memory, TAMA_CLOCK_HOUR_LOW) |
            (GET_RAM_MEMORY(state->memory, TAMA_CLOCK_HOUR_HIGH) << 4);
        const uint8_t second_after =
            GET_RAM_MEMORY(state->memory, TAMA_CLOCK_SECOND_ONES) +
            10u * GET_RAM_MEMORY(state->memory, TAMA_CLOCK_SECOND_TENS);
        if (second_before == second_after) {
            if (sampled_hour >= 24 || sampled_minute >= 60 ||
                second_after >= 60) {
                return false;
            }
            *hour = sampled_hour;
            *minute = sampled_minute;
            *second = second_after;
            return true;
        }
    }
    return false;
}

static void queue_time_sync(int64_t epoch_seconds, void *user) {
    tama_runtime_t *runtime = user;
    if (!runtime || epoch_seconds <= 0) return;
    portENTER_CRITICAL(&s_time_mux);
    runtime->pending_sync_epoch = epoch_seconds;
    runtime->time_sync_pending = true;
    portEXIT_CRITICAL(&s_time_mux);
}

static void apply_pending_time_sync(tama_runtime_t *runtime) {
    int64_t epoch = 0;
    portENTER_CRITICAL(&s_time_mux);
    if (runtime->time_sync_pending) {
        epoch = runtime->pending_sync_epoch;
        runtime->time_sync_pending = false;
    }
    portEXIT_CRITICAL(&s_time_mux);
    if (epoch <= 0) return;

    const int64_t now_us = esp_timer_get_time();
    const int64_t previous_epoch = runtime_state_epoch(runtime, now_us);
    runtime->catchup_active = false;
    runtime->catchup_ticks_remaining = 0;
    runtime->catchup_ticks_total = 0;
    runtime->time_anchor_epoch = epoch;
    runtime->time_anchor_us = now_us;
    runtime->catchup_target_epoch = epoch;

    if (previous_epoch <= 0) {
        ESP_LOGI(TAG, "First trustworthy time acquired; future power-off gaps "
                      "can be recovered");
        set_game_clock(epoch);
        save_state(runtime);
        return;
    }
    if (epoch <= previous_epoch + 5) {
        ESP_LOGI(TAG, "Clock corrected by %lld seconds; no catch-up needed",
                 (long long)(epoch - previous_epoch));
        set_game_clock(epoch);
        save_state(runtime);
        return;
    }

    uint64_t elapsed_seconds = (uint64_t)(epoch - previous_epoch);
    if (elapsed_seconds > TAMA_MAX_CATCHUP_SECONDS) {
        const uint64_t skipped_seconds =
            elapsed_seconds - TAMA_MAX_CATCHUP_SECONDS;
        ESP_LOGW(TAG, "Offline gap is long; advancing wall clock by %llus "
                      "without simulation, then catching up the final %us",
                 (unsigned long long)skipped_seconds,
                 TAMA_MAX_CATCHUP_SECONDS);
        elapsed_seconds = TAMA_MAX_CATCHUP_SECONDS;
    }
    if (elapsed_seconds <= 5) {
        set_game_clock(epoch);
        save_state(runtime);
        return;
    }

    runtime->catchup_ticks_remaining =
        elapsed_seconds * (uint64_t)TAMA_TICK_FREQUENCY;
    runtime->catchup_ticks_total = runtime->catchup_ticks_remaining;
    runtime->catchup_started_us = now_us;
    runtime->last_catchup_render_us = 0;
    runtime->catchup_active = true;
    tamalib_set_speed(0);
    cpu_sync_ref_timestamp();
    ESP_LOGI(TAG, "Catching up %llu offline seconds at maximum speed",
             (unsigned long long)elapsed_seconds);
}

static void run_catchup_batch(tama_runtime_t *runtime) {
    if (!runtime->catchup_active || !runtime->catchup_ticks_remaining) return;
    state_t *state = tamalib_get_state();
    uint32_t previous_tick = *state->tick_counter;
    for (unsigned i = 0; i < TAMA_CATCHUP_BATCH_STEPS; ++i) tamalib_step();
    uint32_t advanced = *state->tick_counter - previous_tick;
    if ((uint64_t)advanced >= runtime->catchup_ticks_remaining) {
        runtime->catchup_ticks_remaining = 0;
    } else {
        runtime->catchup_ticks_remaining -= advanced;
    }
    if (runtime->catchup_ticks_remaining) return;

    runtime->catchup_active = false;
    tamalib_set_speed(1);
    cpu_sync_ref_timestamp();
    runtime->time_anchor_us = esp_timer_get_time();
    // Fast-forward itself takes a few real seconds. Include that time instead
    // of restoring the exact timestamp received at the start of catch-up.
    const int64_t catchup_real_seconds =
        (runtime->time_anchor_us - runtime->catchup_started_us) / 1000000LL;
    runtime->time_anchor_epoch =
        runtime->catchup_target_epoch + catchup_real_seconds;
    set_game_clock(runtime->time_anchor_epoch);
    __atomic_store_n(&runtime->lcd_dirty_rows, 0xffffu, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->icon_dirty_mask, 0xffu, __ATOMIC_RELEASE);
    ESP_LOGI(TAG, "Offline catch-up complete: virtual=%llus real=%lldms",
             (unsigned long long)(runtime->catchup_ticks_total /
                                  TAMA_TICK_FREQUENCY),
             (long long)((runtime->time_anchor_us - runtime->catchup_started_us) /
                         1000));
    save_state(runtime);
}

static int tama_handler(void) {
    tama_runtime_t *runtime = s_runtime;
    if (!runtime) return 1;
    const uint32_t pressed = __atomic_exchange_n(&runtime->pending_pressed, 0,
                                                  __ATOMIC_ACQ_REL);
    const uint32_t released = __atomic_exchange_n(&runtime->pending_released, 0,
                                                   __ATOMIC_ACQ_REL);
    static const button_t tama_button[3] = {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE};
    static const uint8_t physical_to_ui[3] = {0, 2, 1};
    // Physical order is Up=A/left, Down=C/right, Confirm=B/middle.
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t bit = 1u << i;
        if (pressed & bit) {
            runtime->button_down[i] = 1;
            tamalib_set_button(tama_button[i], BTN_STATE_PRESSED);
            __atomic_fetch_or(&runtime->button_dirty_mask,
                              1u << physical_to_ui[i], __ATOMIC_RELEASE);
        }
        if (released & bit) {
            runtime->button_down[i] = 0;
            tamalib_set_button(tama_button[i], BTN_STATE_RELEASED);
            __atomic_fetch_or(&runtime->button_dirty_mask,
                              1u << physical_to_ui[i], __ATOMIC_RELEASE);
        }
    }

    apply_pending_time_sync(runtime);
    run_catchup_batch(runtime);

    int64_t now = esp_timer_get_time();
    if (now >= runtime->next_save_us &&
        now - runtime->last_save_us >= TAMA_AUTOSAVE_US) {
        save_state(runtime);
    }
    if (now - runtime->ui_metrics_started_us >= 5LL * 1000LL * 1000LL) {
        if (runtime->ui_update_count) {
            ESP_LOGI(TAG, "UI %u updates avg=%lluus bytes/update=%u",
                     (unsigned)runtime->ui_update_count,
                     (unsigned long long)(runtime->ui_time_us /
                                          runtime->ui_update_count),
                     (unsigned)(runtime->ui_bytes / runtime->ui_update_count));
        }
        runtime->ui_update_count = 0;
        runtime->ui_bytes = 0;
        runtime->ui_time_us = 0;
        runtime->ui_metrics_started_us = now;
    }
    if (now - runtime->last_yield_us >= 10000) {
        runtime->last_yield_us = now;
        vTaskDelay(1);
    }
    return 0;
}

static hal_t TAMA_HAL = {
    .malloc = tama_malloc,
    .free = tama_free,
    .halt = tama_halt,
    .is_log_enabled = tama_is_log_enabled,
    .log = tama_log,
    .sleep_until = tama_sleep_until,
    .get_timestamp = tama_timestamp,
    .update_screen = tama_update_screen,
    .set_lcd_matrix = tama_set_lcd_matrix,
    .set_lcd_icon = tama_set_lcd_icon,
    .set_frequency = tama_set_frequency,
    .play_frequency = tama_play_frequency,
    .handler = tama_handler,
};

static void on_button(int index, hal_btn_event_t event, void *user) {
    tama_runtime_t *runtime = user;
    if (!runtime || index < 0 || index >= 3) return;
    uint32_t bit = 1u << (unsigned)index;
    if (event == HAL_BTN_PRESS) {
        ESP_LOGI(TAG, "Button index=%d pressed", index);
        __atomic_fetch_or(&runtime->pending_pressed, bit, __ATOMIC_RELEASE);
    } else if (event == HAL_BTN_RELEASE) {
        ESP_LOGI(TAG, "Button index=%d released", index);
        __atomic_fetch_or(&runtime->pending_released, bit, __ATOMIC_RELEASE);
    }
}

static bool load_program(const esp_partition_t *partition, u12_t **program_out,
                         uint32_t *crc_out) {
    if (!partition || partition->size != TAMA_ROM_BYTES) return false;
    const void *mapped = NULL;
    esp_partition_mmap_handle_t mapping = 0;
    if (esp_partition_mmap(partition, 0, TAMA_ROM_BYTES,
                           ESP_PARTITION_MMAP_DATA, &mapped, &mapping) != ESP_OK) {
        return false;
    }
    const uint8_t *bytes = mapped;
    bool valid = false;
    for (unsigned i = 0; i < TAMA_PROGRAM_WORDS; ++i) {
        if ((bytes[i * 2u] & 0xf0u) != 0) {
            esp_partition_munmap(mapping);
            return false;
        }
        if (bytes[i * 2u] != 0xffu || bytes[i * 2u + 1u] != 0xffu) valid = true;
    }
    if (!valid) {
        esp_partition_munmap(mapping);
        return false;
    }
    u12_t *program = heap_caps_malloc(TAMA_PROGRAM_WORDS * sizeof(u12_t),
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!program) {
        esp_partition_munmap(mapping);
        return false;
    }
    for (unsigned i = 0; i < TAMA_PROGRAM_WORDS; ++i) {
        program[i] = (u12_t)(((bytes[i * 2u] & 0x0fu) << 8) |
                             bytes[i * 2u + 1u]);
    }
    *crc_out = esp_rom_crc32_le(0, bytes, TAMA_ROM_BYTES);
    *program_out = program;
    esp_partition_munmap(mapping);
    return true;
}

bool platform_tamagotchi_try_boot(const board_config_t *board) {
    if (!board) return false;
    const esp_partition_t *rom_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, TAMA_ROM_LABEL);
    tama_runtime_t runtime = {
        .save_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                   ESP_PARTITION_SUBTYPE_ANY,
                                                   TAMA_SAVE_LABEL),
        .save_active_slot = -1,
    };
    uint32_t rom_crc = 0;
    if (!load_program(rom_partition, &runtime.program, &rom_crc)) {
        ESP_LOGE(TAG, "Partition '%s' does not contain a valid 12 KiB P1 ROM",
                 TAMA_ROM_LABEL);
        return false;
    }

    runtime.render_buffer = heap_caps_malloc(
        TAMA_SCREEN_WIDTH * TAMA_RENDER_LINES * sizeof(uint16_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    runtime.display = platform_create_display(board);
    hal_button_t *button = platform_create_button(board);
    if (!runtime.render_buffer || !runtime.display || !button) {
        ESP_LOGE(TAG, "Display/button allocation failed (heap=%u)",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        return false;
    }
    runtime.display_io = platform_display_io(runtime.display);
    runtime.display_task = xTaskGetCurrentTaskHandle();
    const esp_lcd_panel_io_callbacks_t display_callbacks = {
        .on_color_trans_done = on_color_trans_done,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(
        runtime.display_io, &display_callbacks, &runtime));
    s_runtime = &runtime;
    tamalib_register_hal(&TAMA_HAL);
    tamalib_set_framerate(TAMA_UI_FPS);
    if (tamalib_init(runtime.program, NULL, 1000000u)) {
        ESP_LOGE(TAG, "TamaLIB initialization failed");
        return false;
    }
    load_state(&runtime);
    hal_button_on_event(button, on_button, &runtime);

    int64_t now = esp_timer_get_time();
    runtime.last_save_us = now;
    runtime.next_save_us = now + TAMA_AUTOSAVE_US;
    runtime.last_yield_us = now;
    runtime.ui_metrics_started_us = now;
    const int64_t initial_render_started = esp_timer_get_time();
    render_screen(&runtime);
    __atomic_store_n(&runtime.lcd_dirty_rows, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime.icon_dirty_mask, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime.button_dirty_mask, 0, __ATOMIC_RELEASE);
    hal_display_set_backlight(runtime.display, 100);

#if PERIPH_AUDIO
    runtime.audio = platform_create_audio(board);
    if (runtime.audio &&
        hal_audio_set_sample_rate(runtime.audio, TAMA_AUDIO_RATE, 16, 1) == 0) {
        hal_audio_set_volume(runtime.audio, 45);
        hal_audio_set_out_mute(runtime.audio, false);
        if (xTaskCreate(audio_task, "tama_audio", 3072, &runtime, 4, NULL) != pdPASS) {
            ESP_LOGW(TAG, "Unable to start Tamagotchi audio task; continuing silent");
        }
    } else {
        ESP_LOGW(TAG, "Tamagotchi audio unavailable; continuing silent");
    }
#endif

    tamagotchi_network_start(queue_time_sync, read_game_clock, &runtime);
    ESP_LOGI(TAG,
             "Tamagotchi P1 started rom=%u bytes crc32=%08x heap=%u; "
             "UI=%u fps initial=%lldms; Up=A, Confirm=B, Down=C; "
             "autosave every 30 seconds; Wi-Fi time sync is non-blocking",
             TAMA_ROM_BYTES, (unsigned)rom_crc,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             TAMA_UI_FPS,
             (long long)((esp_timer_get_time() - initial_render_started) / 1000));
    tamalib_mainloop();
    save_state(&runtime);
    return true;
}

#else

#include "platform/platform_factory.h"

bool platform_tamagotchi_try_boot(const board_config_t *board) {
    (void)board;
    return false;
}

#endif
