// components/platform/platform_esp32/src/jpeg_store.c —— imgstore 分区: header + JPG data
#include "platform/board_config.h"
#if PERIPH_DISPLAY
#include "jpeg_store.h"
#include "esp_partition.h"
#include <string.h>

#define HDR_OFF   0
#define HDR_SZ    0x1000                 // 1 sector 保留给 header
#define DATA_OFF  0x1000
static const char MAGIC[4] = {'J','P','G','1'};

static uint32_t s_wr;                    // 当前写偏移(相对 DATA_OFF)
static esp_partition_mmap_handle_t s_mh;
static bool s_mapped;
static jpeg_slot_t s_wr_slot = JPEG_SLOT_FULL;   // begin 记住写入槽,write/end 沿用

static const char *slot_part_name(jpeg_slot_t s) {
    (void)s;
    return "imgstore";
}
static const esp_partition_t *part_of(jpeg_slot_t s) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, slot_part_name(s));
}
static uint32_t rd_len(const esp_partition_t *p) {
    uint8_t h[8];
    if (esp_partition_read(p, HDR_OFF, h, sizeof h) != ESP_OK) return 0;
    if (memcmp(h, MAGIC, 4) != 0) return 0;
    return (uint32_t)h[4] | ((uint32_t)h[5]<<8) | ((uint32_t)h[6]<<16) | ((uint32_t)h[7]<<24);
}

int jpeg_store_begin(jpeg_slot_t slot, uint32_t total_len) {
    const esp_partition_t *p = part_of(slot); if (!p) return -2;
    uint32_t cap = p->size - DATA_OFF;
    if (total_len > cap) return -1;
    uint32_t data_erase = (total_len + 0xFFFu) & ~0xFFFu;   // 向上取整到 sector
    if (esp_partition_erase_range(p, 0, HDR_SZ + data_erase) != ESP_OK) return -2;
    s_wr = 0;
    s_wr_slot = slot;
    return 0;
}
int jpeg_store_write(const uint8_t *d, int n) {
    const esp_partition_t *p = part_of(s_wr_slot); if (!p) return -2;
    if (esp_partition_write(p, DATA_OFF + s_wr, d, (size_t)n) != ESP_OK) return -2;
    s_wr += (uint32_t)n;
    return 0;
}
int jpeg_store_end(void) {
    const esp_partition_t *p = part_of(s_wr_slot); if (!p) return -2;
    uint8_t h[8];
    memcpy(h, MAGIC, 4);
    h[4]=s_wr&0xFF; h[5]=(s_wr>>8)&0xFF; h[6]=(s_wr>>16)&0xFF; h[7]=(s_wr>>24)&0xFF;
    if (esp_partition_write(p, HDR_OFF, h, sizeof h) != ESP_OK) return -2;
    return 0;
}
bool jpeg_store_has_valid(jpeg_slot_t slot) {
    const esp_partition_t *p = part_of(slot); if (!p) return false;
    uint32_t l = rd_len(p);
    return l > 0 && l <= (p->size - DATA_OFF);
}
int jpeg_store_mmap(jpeg_slot_t slot, const uint8_t **ptr, int *len) {
    const esp_partition_t *p = part_of(slot); if (!p) return -2;
    uint32_t l = rd_len(p);
    if (l == 0 || l > (p->size - DATA_OFF)) return -2;
    if (s_mapped) { esp_partition_munmap(s_mh); s_mapped = false; }
    const void *m = NULL;
    if (esp_partition_mmap(p, DATA_OFF, l, ESP_PARTITION_MMAP_DATA, &m, &s_mh) != ESP_OK) return -2;
    s_mapped = true;
    *ptr = (const uint8_t *)m; *len = (int)l;
    return 0;
}
void jpeg_store_unmap(void) {
    if (s_mapped) { esp_partition_munmap(s_mh); s_mapped = false; }
}

// ---- 解码后 RGB565 整帧存储(imgframe 分区):供 lv_image 从 flash 显示,不占整帧 RAM ----
static esp_partition_mmap_handle_t s_fmh;
static bool s_fmapped;
static const esp_partition_t *frame_part(void) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "imgframe");
}
int jpeg_frame_begin(uint32_t nbytes) {
    const esp_partition_t *p = frame_part(); if (!p) return -2;
    if (nbytes > p->size) return -1;
    uint32_t er = (nbytes + 0xFFFu) & ~0xFFFu;   // 向上取整到 sector
    if (esp_partition_erase_range(p, 0, er) != ESP_OK) return -2;
    return 0;
}
int jpeg_frame_write(uint32_t off, const uint8_t *d, int n) {
    const esp_partition_t *p = frame_part(); if (!p) return -2;
    if (esp_partition_write(p, off, d, (size_t)n) != ESP_OK) return -2;
    return 0;
}
int jpeg_frame_mmap(const uint8_t **ptr, uint32_t nbytes) {
    const esp_partition_t *p = frame_part(); if (!p) return -2;
    if (s_fmapped) { esp_partition_munmap(s_fmh); s_fmapped = false; }
    const void *m = NULL;
    if (esp_partition_mmap(p, 0, nbytes, ESP_PARTITION_MMAP_DATA, &m, &s_fmh) != ESP_OK) return -2;
    s_fmapped = true; *ptr = (const uint8_t *)m;
    return 0;
}
void jpeg_frame_unmap(void) {
    if (s_fmapped) { esp_partition_munmap(s_fmh); s_fmapped = false; }
}
uint32_t jpeg_frame_capacity(void) {
    const esp_partition_t *p = frame_part();
    return p ? p->size : 0;
}
#endif // PERIPH_DISPLAY
