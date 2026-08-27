#include "platform/board_config.h"

#if PERIPH_DISPLAY && PERIPH_BUTTON

#include "platform/platform_factory.h"

#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_sys.h"
#include "esp_rom_crc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nofrendo_fast/nofrendo_fast.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NES_ROM_LABEL "nesrom"
#define NES_SAVE_LABEL "nessave"
#define NES_RENDER_LINES 20u
#define NES_LCD_WIDTH 240u
#define NES_LCD_Y_OFFSET 40u
#define NES_SAVE_SLOT_SIZE 0x3000u
#define NES_SAVE_SLOT_COUNT 2u
#define NES_SAVE_MAGIC 0x3153454eu /* "NES1" in little endian */
#define NES_SAVE_POLL_US (10LL * 1000LL * 1000LL)

static const char *TAG = "nes_runtime";

// platform_display_io is intentionally private to the ESP32 platform layer.
// A polling parameter transaction drains all queued color DMA transfers before
// the single render buffer is reused.
esp_lcd_panel_io_handle_t platform_display_io(hal_display_t *display);

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t generation;
    uint32_t data_size;
    uint32_t crc32;
} nes_save_header_t;

typedef struct {
    nofrendo_fast_t *core;
    hal_display_t *display;
    esp_lcd_panel_io_handle_t display_io;
    const esp_partition_t *save_partition;
    uint16_t *render_buffer;
    uint16_t palette[256];
    TaskHandle_t display_task;
    volatile uint32_t display_completed;
    uint32_t display_submitted;
    uint32_t buffer_sequence[2];
    bool display_failed;
    volatile uint32_t pending_input;
    volatile bool exit_requested;
    uint32_t save_generation;
    int save_active_slot;
    uint32_t persisted_crc;
    uint32_t candidate_crc;
    unsigned candidate_samples;
    uint64_t scale_time_us;
    uint64_t lcd_time_us;
} nes_runtime_t;

enum {
    NES_PENDING_A      = 1u << 0,
    NES_PENDING_B      = 1u << 1,
    NES_PENDING_SELECT = 1u << 2,
    NES_PENDING_START  = 1u << 3,
    NES_PENDING_UP     = 1u << 4,
    NES_PENDING_DOWN   = 1u << 5,
    NES_PENDING_LEFT   = 1u << 6,
    NES_PENDING_RIGHT  = 1u << 7,
};

static bool inspect_ines_header(const uint8_t header[16], size_t capacity,
                                size_t *rom_size, unsigned *mapper) {
    static const uint8_t magic[4] = {'N', 'E', 'S', 0x1a};
    if (memcmp(header, magic, sizeof(magic)) != 0 || header[4] == 0) {
        return false;
    }
    // Agnes currently consumes the original iNES 1.0 size fields only.
    if ((header[7] & 0x0cu) == 0x08u) {
        ESP_LOGW(TAG, "NES 2.0 image is not supported yet");
        return false;
    }
    const size_t trainer_size = (header[6] & 0x04u) ? 512u : 0u;
    const size_t prg_size = (size_t)header[4] * 16u * 1024u;
    const size_t chr_size = (size_t)header[5] * 8u * 1024u;
    const size_t total = 16u + trainer_size + prg_size + chr_size;
    const unsigned mapper_number = (unsigned)(header[6] >> 4) | (unsigned)(header[7] & 0xf0u);
    if (total > capacity ||
        (mapper_number != 0u && mapper_number != 1u &&
         mapper_number != 2u && mapper_number != 4u)) {
        return false;
    }
    *rom_size = total;
    *mapper = mapper_number;
    return true;
}

static uint16_t rgb565_be(nofrendo_fast_color_t color) {
    const uint16_t pixel = (uint16_t)(((uint16_t)(color.r & 0xf8u) << 8) |
                                      ((uint16_t)(color.g & 0xfcu) << 3) |
                                      ((uint16_t)color.b >> 3));
    return __builtin_bswap16(pixel);
}

static void wait_for_display(nes_runtime_t *runtime) {
    // NOP is a synchronous polling transaction. ESP-IDF guarantees it first
    // drains pending esp_lcd color transactions, making one DMA buffer safe.
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(runtime->display_io, 0x00, NULL, 0));
}

static void clear_display(nes_runtime_t *runtime) {
    memset(runtime->render_buffer, 0,
           NES_LCD_WIDTH * NES_RENDER_LINES * sizeof(runtime->render_buffer[0]));
    for (unsigned y = 0; y < 320u; y += NES_RENDER_LINES) {
        hal_display_flush(runtime->display, 0, (int)y, NES_LCD_WIDTH - 1,
                          (int)(y + NES_RENDER_LINES - 1), runtime->render_buffer);
        wait_for_display(runtime);
    }
}

static inline __attribute__((always_inline)) void convert_row_rgb565(
    uint32_t *restrict destination, const uint8_t *restrict source,
    const uint16_t *restrict palette) {
    // The DMA buffer and 240-pixel stride are 32-bit aligned. Packing two
    // pre-byte-swapped RGB565 pixels per store halves the loop and store count.
    for (unsigned pair = 0; pair < NES_LCD_WIDTH / 2u; pair += 4u) {
        destination[pair] = (uint32_t)palette[source[0]] |
                            ((uint32_t)palette[source[1]] << 16);
        destination[pair + 1u] = (uint32_t)palette[source[2]] |
                                 ((uint32_t)palette[source[3]] << 16);
        destination[pair + 2u] = (uint32_t)palette[source[4]] |
                                 ((uint32_t)palette[source[5]] << 16);
        destination[pair + 3u] = (uint32_t)palette[source[6]] |
                                 ((uint32_t)palette[source[7]] << 16);
        source += 8;
    }
}

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                esp_lcd_panel_io_event_data_t *event,
                                void *user) {
    (void)io;
    (void)event;
    nes_runtime_t *runtime = user;
    __atomic_add_fetch(&runtime->display_completed, 1u, __ATOMIC_RELEASE);
    BaseType_t task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(runtime->display_task, &task_woken);
    return task_woken == pdTRUE;
}

static void wait_for_sequence(nes_runtime_t *runtime, uint32_t sequence) {
    while (__atomic_load_n(&runtime->display_completed, __ATOMIC_ACQUIRE) < sequence) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

static bool prepare_stream_window(nes_runtime_t *runtime) {
    const uint8_t columns[] = {0x00, 0x00, 0x00, NES_LCD_WIDTH - 1u};
    const unsigned last_row = NES_LCD_Y_OFFSET + NOFRENDO_FAST_HEIGHT - 1u;
    const uint8_t rows[] = {
        (uint8_t)(NES_LCD_Y_OFFSET >> 8), (uint8_t)NES_LCD_Y_OFFSET,
        (uint8_t)(last_row >> 8), (uint8_t)last_row,
    };
    return esp_lcd_panel_io_tx_param(runtime->display_io, 0x2a,
                                     columns, sizeof(columns)) == ESP_OK &&
           esp_lcd_panel_io_tx_param(runtime->display_io, 0x2b,
                                     rows, sizeof(rows)) == ESP_OK;
}

static void stream_scanlines(void *user, unsigned y0, unsigned line_count) {
    nes_runtime_t *runtime = user;
    if (runtime->display_failed || line_count != NES_RENDER_LINES ||
        y0 + line_count > NOFRENDO_FAST_HEIGHT) {
        runtime->display_failed = true;
        return;
    }
    size_t pitch = 0;
    const uint8_t *frame = nofrendo_fast_framebuffer(runtime->core, &pitch);
    if (!frame || pitch < NOFRENDO_FAST_WIDTH) {
        runtime->display_failed = true;
        return;
    }

    const unsigned chunk = y0 / NES_RENDER_LINES;
    const unsigned buffer_index = chunk & 1u;
    const int64_t lcd_started = esp_timer_get_time();
    wait_for_sequence(runtime, runtime->buffer_sequence[buffer_index]);
    runtime->lcd_time_us += (uint64_t)(esp_timer_get_time() - lcd_started);

    const int64_t scale_started = esp_timer_get_time();
    uint16_t *chunk_buffer = runtime->render_buffer +
        buffer_index * NES_LCD_WIDTH * NES_RENDER_LINES;
    for (unsigned row = 0; row < line_count; ++row) {
        const uint8_t *source = frame + (y0 + row) * pitch + 8u;
        uint16_t *destination = chunk_buffer + row * NES_LCD_WIDTH;
        // NES software expects horizontal TV overscan. Cropping eight pixels
        // per side is sharper and cheaper than 256->240 resampling.
        convert_row_rgb565((uint32_t *)destination, source, runtime->palette);
    }
    runtime->scale_time_us += (uint64_t)(esp_timer_get_time() - scale_started);

    // RAMWR starts a new frame. Later chunks are raw data-only transactions,
    // so the ST7789 address pointer continues through the 240x240 window.
    const int command = y0 == 0 ? 0x2c : -1;
    if (esp_lcd_panel_io_tx_color(runtime->display_io, command, chunk_buffer,
                                  NES_LCD_WIDTH * line_count * sizeof(uint16_t)) != ESP_OK) {
        runtime->display_failed = true;
        return;
    }
    const uint32_t sequence = ++runtime->display_submitted;
    runtime->buffer_sequence[buffer_index] = sequence;
}

static void on_button(int index, hal_btn_event_t event, void *user) {
    nes_runtime_t *runtime = user;
    if (!runtime || event == HAL_BTN_PRESS) {
        return;
    }
    uint32_t input = 0;
    if (index == 0) {
        if (event == HAL_BTN_CLICK) input = NES_PENDING_UP;
        else if (event == HAL_BTN_DOUBLE) input = NES_PENDING_LEFT;
        else if (event == HAL_BTN_LONG) input = NES_PENDING_SELECT;
    } else if (index == 1) {
        if (event == HAL_BTN_CLICK) input = NES_PENDING_DOWN;
        else if (event == HAL_BTN_DOUBLE) input = NES_PENDING_RIGHT;
        else if (event == HAL_BTN_LONG) {
            __atomic_store_n(&runtime->exit_requested, true, __ATOMIC_RELEASE);
            return;
        }
    } else if (index == 2) {
        if (event == HAL_BTN_CLICK) input = NES_PENDING_A;
        else if (event == HAL_BTN_DOUBLE) input = NES_PENDING_B;
        else if (event == HAL_BTN_LONG) input = NES_PENDING_START;
    }
    if (input) {
        __atomic_fetch_or(&runtime->pending_input, input, __ATOMIC_RELEASE);
    }
}

static bool generation_is_newer(uint32_t lhs, uint32_t rhs) {
    return (int32_t)(lhs - rhs) > 0;
}

static bool read_save_slot(const esp_partition_t *partition, unsigned slot,
                           nes_save_header_t *header, uint8_t *data, size_t data_size) {
    const size_t offset = (size_t)slot * NES_SAVE_SLOT_SIZE;
    if (esp_partition_read(partition, offset, header, sizeof(*header)) != ESP_OK ||
        header->magic != NES_SAVE_MAGIC || header->data_size != data_size ||
        sizeof(*header) + data_size > NES_SAVE_SLOT_SIZE ||
        esp_partition_read(partition, offset + sizeof(*header), data, data_size) != ESP_OK) {
        return false;
    }
    return esp_rom_crc32_le(0, data, (uint32_t)data_size) == header->crc32;
}

static void load_save_ram(nes_runtime_t *runtime) {
    const size_t save_size = nofrendo_fast_save_ram_size(runtime->core);
    if (!save_size || !runtime->save_partition ||
        runtime->save_partition->size < NES_SAVE_SLOT_SIZE * NES_SAVE_SLOT_COUNT) {
        return;
    }
    uint8_t *data = malloc(save_size);
    if (!data) {
        ESP_LOGW(TAG, "No heap for %u-byte save restore", (unsigned)save_size);
        return;
    }
    nes_save_header_t headers[NES_SAVE_SLOT_COUNT];
    bool valid[NES_SAVE_SLOT_COUNT] = {false, false};
    for (unsigned slot = 0; slot < NES_SAVE_SLOT_COUNT; ++slot) {
        valid[slot] = read_save_slot(runtime->save_partition, slot, &headers[slot],
                                     data, save_size);
    }
    int selected = -1;
    if (valid[0]) selected = 0;
    if (valid[1] && (selected < 0 ||
                     generation_is_newer(headers[1].generation, headers[selected].generation))) {
        selected = 1;
    }
    if (selected >= 0 &&
        read_save_slot(runtime->save_partition, (unsigned)selected, &headers[selected],
                       data, save_size) &&
        nofrendo_fast_load_save_ram(runtime->core, data, save_size)) {
        runtime->save_generation = headers[selected].generation;
        runtime->save_active_slot = selected;
        runtime->persisted_crc = headers[selected].crc32;
        ESP_LOGI(TAG, "Restored %u-byte battery save, generation=%u slot=%d",
                 (unsigned)save_size, (unsigned)runtime->save_generation, selected);
    } else {
        // Agnes zero-initializes Mapper RAM. Treat that state as persisted so
        // an untouched game doesn't create a flash save needlessly.
        memset(data, 0, save_size);
        runtime->persisted_crc = esp_rom_crc32_le(0, data, (uint32_t)save_size);
    }
    free(data);
}

static bool save_ram(nes_runtime_t *runtime) {
    const size_t save_size = nofrendo_fast_save_ram_size(runtime->core);
    if (!save_size || !runtime->save_partition ||
        runtime->save_partition->size < NES_SAVE_SLOT_SIZE * NES_SAVE_SLOT_COUNT) {
        return false;
    }
    uint8_t *data = malloc(save_size);
    if (!data || !nofrendo_fast_copy_save_ram(runtime->core, data, save_size)) {
        free(data);
        return false;
    }
    const uint32_t crc = esp_rom_crc32_le(0, data, (uint32_t)save_size);
    if (crc == runtime->persisted_crc) {
        free(data);
        return true;
    }
    const unsigned slot = runtime->save_active_slot < 0
                              ? 0u
                              : ((unsigned)runtime->save_active_slot + 1u) % NES_SAVE_SLOT_COUNT;
    const size_t offset = (size_t)slot * NES_SAVE_SLOT_SIZE;
    const nes_save_header_t header = {
        .magic = NES_SAVE_MAGIC,
        .generation = runtime->save_generation + 1u,
        .data_size = (uint32_t)save_size,
        .crc32 = crc,
    };
    // Data first, header last: a power loss can invalidate only the new slot;
    // the previous generation remains readable.
    esp_err_t error = esp_partition_erase_range(runtime->save_partition, offset,
                                                 NES_SAVE_SLOT_SIZE);
    if (error == ESP_OK) {
        error = esp_partition_write(runtime->save_partition, offset + sizeof(header),
                                    data, save_size);
    }
    if (error == ESP_OK) {
        error = esp_partition_write(runtime->save_partition, offset, &header, sizeof(header));
    }
    if (error == ESP_OK) {
        nes_save_header_t verify_header;
        error = read_save_slot(runtime->save_partition, slot, &verify_header, data, save_size)
                    ? ESP_OK
                    : ESP_ERR_INVALID_CRC;
    }
    free(data);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Battery save failed: %s", esp_err_to_name(error));
        return false;
    }
    runtime->save_generation = header.generation;
    runtime->save_active_slot = (int)slot;
    runtime->persisted_crc = crc;
    runtime->candidate_samples = 0;
    ESP_LOGI(TAG, "Saved battery RAM, generation=%u slot=%u",
             (unsigned)header.generation, slot);
    return true;
}

static void poll_stable_save(nes_runtime_t *runtime) {
    const size_t save_size = nofrendo_fast_save_ram_size(runtime->core);
    if (!save_size) return;
    uint8_t *data = malloc(save_size);
    if (!data || !nofrendo_fast_copy_save_ram(runtime->core, data, save_size)) {
        free(data);
        return;
    }
    const uint32_t crc = esp_rom_crc32_le(0, data, (uint32_t)save_size);
    free(data);
    if (crc == runtime->persisted_crc) {
        runtime->candidate_samples = 0;
    } else if (runtime->candidate_samples && crc == runtime->candidate_crc) {
        if (++runtime->candidate_samples >= 2u) save_ram(runtime);
    } else {
        runtime->candidate_crc = crc;
        runtime->candidate_samples = 1u;
    }
}

static uint8_t input_from_pending(uint32_t pending) {
    return (uint8_t)pending;
}

bool platform_nes_try_boot(const board_config_t *board, int boot_button_index) {
    (void)boot_button_index;
    if (!board) {
        return false;
    }
    const esp_partition_t *rom_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NES_ROM_LABEL);
    if (!rom_partition) {
        ESP_LOGW(TAG, "NES auto-boot skipped: partition '%s' is missing", NES_ROM_LABEL);
        return false;
    }
    uint8_t header[16];
    size_t rom_size = 0;
    unsigned mapper = 0;
    if (esp_partition_read(rom_partition, 0, header, sizeof(header)) != ESP_OK ||
        !inspect_ines_header(header, rom_partition->size, &rom_size, &mapper)) {
        ESP_LOGW(TAG, "NES auto-boot skipped: '%s' has no supported iNES image",
                 NES_ROM_LABEL);
        return false;
    }
    const void *rom = NULL;
    esp_partition_mmap_handle_t rom_mapping = 0;
    if (esp_partition_mmap(rom_partition, 0, rom_size, ESP_PARTITION_MMAP_DATA,
                           &rom, &rom_mapping) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to memory-map NES ROM");
        return false;
    }

    nes_runtime_t runtime = {
        .save_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                   ESP_PARTITION_SUBTYPE_ANY,
                                                   NES_SAVE_LABEL),
        .save_active_slot = -1,
    };
    runtime.core = nofrendo_fast_create(rom, rom_size);
    runtime.render_buffer = heap_caps_malloc(
        2u * NES_LCD_WIDTH * NES_RENDER_LINES * sizeof(runtime.render_buffer[0]),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!runtime.core || !runtime.render_buffer) {
        ESP_LOGE(TAG, "NES core allocation/load failed (free heap=%u)",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        if (runtime.render_buffer) free(runtime.render_buffer);
        if (runtime.core) nofrendo_fast_destroy(runtime.core);
        esp_partition_munmap(rom_mapping);
        return false;
    }
    runtime.display = platform_create_display(board);
    hal_button_t *button = platform_create_button(board);
    if (!runtime.display || !button) {
        ESP_LOGE(TAG, "NES display/button initialization failed");
        esp_restart();
    }
    runtime.display_io = platform_display_io(runtime.display);
    for (unsigned i = 0; i < 256u; ++i) {
        runtime.palette[i] = rgb565_be(nofrendo_fast_palette_color(i));
    }
    hal_button_on_event(button, on_button, &runtime);
    clear_display(&runtime);
    runtime.display_task = xTaskGetCurrentTaskHandle();
    const esp_lcd_panel_io_callbacks_t display_callbacks = {
        .on_color_trans_done = on_color_trans_done,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(
        runtime.display_io, &display_callbacks, &runtime));
    if (!prepare_stream_window(&runtime)) {
        ESP_LOGE(TAG, "Unable to configure NES LCD stream window");
        esp_restart();
    }
    hal_display_set_backlight(runtime.display, 100);
    load_save_ram(&runtime);

    ESP_LOGI(TAG,
             "NES started with scanline core: mapper=%u rom=%u bytes heap=%u; "
             "Up/Down=move, double Up/Down=Left/Right, OK=A, double OK=B, "
             "long OK=Start, long Up=Select, long Down=save+exit",
             mapper, (unsigned)rom_size,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    unsigned held_frames = 0;
    uint8_t input = 0;
    int64_t next_save_poll = esp_timer_get_time() + NES_SAVE_POLL_US;
    uint32_t frames = 0;
    uint32_t frame_number = 0;
    uint64_t core_time_us = 0;
    int64_t fps_started = esp_timer_get_time();
    int64_t next_frame_deadline = fps_started;
    while (!__atomic_load_n(&runtime.exit_requested, __ATOMIC_ACQUIRE)) {
        const uint32_t pending = __atomic_exchange_n(&runtime.pending_input, 0,
                                                      __ATOMIC_ACQ_REL);
        if (pending) {
            input = input_from_pending(pending);
            held_frames = 3;
        } else if (held_frames && --held_frames == 0) {
            input = 0;
        }
        const int64_t core_started = esp_timer_get_time();
        if (!nofrendo_fast_run_frame_streamed(runtime.core, input,
                                               stream_scanlines, &runtime)) {
            ESP_LOGE(TAG, "NES CPU stopped");
            break;
        }
        core_time_us += (uint64_t)(esp_timer_get_time() - core_started);
        if (runtime.display_failed) {
            ESP_LOGE(TAG, "NES streamed display failed");
            break;
        }
        ++frames;
        ++frame_number;
        const int64_t now = esp_timer_get_time();
        if (now >= next_save_poll) {
            poll_stable_save(&runtime);
            next_save_poll = now + NES_SAVE_POLL_US;
        }
        if (now - fps_started >= 5LL * 1000LL * 1000LL) {
            const double fps = (double)frames * 1000000.0 / (double)(now - fps_started);
            const uint64_t divisor = frames ? frames : 1u;
            ESP_LOGI(TAG,
                     "%.1f FPS stream=%" PRIu64 "us/f lcdwait=%" PRIu64
                     "us/f scale=%" PRIu64 "us/f heap=%u",
                     fps, core_time_us / divisor, runtime.lcd_time_us / divisor,
                     runtime.scale_time_us / divisor,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            frames = 0;
            core_time_us = 0;
            runtime.lcd_time_us = 0;
            runtime.scale_time_us = 0;
            fps_started = now;
        }
        // Pace against an absolute NTSC deadline, so an occasional scheduler
        // tick doesn't accumulate drift. Let Idle run twice a second to feed
        // its watchdog; the following frame naturally catches up.
        next_frame_deadline += 16667;
        if (frame_number % 30u == 0u) {
            vTaskDelay(1);
        }
        int64_t remaining_us = next_frame_deadline - esp_timer_get_time();
        if (remaining_us > 1500) {
            vTaskDelay(pdMS_TO_TICKS((uint32_t)((remaining_us - 500) / 1000)));
            remaining_us = next_frame_deadline - esp_timer_get_time();
        }
        if (remaining_us > 0) {
            esp_rom_delay_us((uint32_t)remaining_us);
        } else if (remaining_us < -16667) {
            next_frame_deadline = esp_timer_get_time();
        }
    }
    save_ram(&runtime);
    wait_for_sequence(&runtime, runtime.display_submitted);
    ESP_LOGI(TAG, "Leaving NES mode; restarting normal firmware");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return true;
}

#else

#include "platform/platform_factory.h"

bool platform_nes_try_boot(const board_config_t *board, int boot_button_index) {
    (void)board;
    (void)boot_button_index;
    return false;
}

#endif
