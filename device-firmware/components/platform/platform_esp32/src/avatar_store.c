#include "avatar_store.h"

#include <limits.h>
#include <stddef.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "services/avatar_pack.h"

static const char *TAG = "avatar_store";
static avatar_pack_t s_pack;
static esp_partition_mmap_handle_t s_handle;
static bool s_mapped;

void avatar_store_deinit(void) {
    if (s_mapped) {
        esp_partition_munmap(s_handle);
        s_mapped = false;
    }
    s_pack = (avatar_pack_t){0};
}

int avatar_store_init(void) {
    avatar_store_deinit();
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "imgava");
    if (partition == NULL) {
        ESP_LOGE(TAG, "imgava partition missing");
        return -1;
    }
    const void *mapped = NULL;
    if (esp_partition_mmap(partition, 0, partition->size,
                           ESP_PARTITION_MMAP_DATA, &mapped, &s_handle) != ESP_OK) {
        ESP_LOGE(TAG, "imgava mmap failed");
        return -2;
    }
    s_mapped = true;
    int result = avatar_pack_init(&s_pack, mapped, partition->size);
    if (result != AVATAR_PACK_OK) {
        ESP_LOGE(TAG, "invalid AVA1 pack rc=%d", result);
        avatar_store_deinit();
        return -3;
    }
    ESP_LOGI(TAG, "AVA1 ready: %u avatars, %lu bytes", s_pack.count,
             (unsigned long)s_pack.total_size);
    return 0;
}

int avatar_store_get(const char *name, const uint8_t **png, int *len) {
    if (png == NULL || len == NULL) {
        return AVATAR_PACK_ERR_ARG;
    }
    size_t size = 0;
    int result = avatar_pack_find(&s_pack, name, png, &size);
    if (result != AVATAR_PACK_OK) {
        return result;
    }
    if (size > INT_MAX) {
        return AVATAR_PACK_ERR_BOUNDS;
    }
    *len = (int)size;
    return AVATAR_PACK_OK;
}

bool avatar_store_has(const char *name) {
    const uint8_t *png = NULL;
    int len = 0;
    return avatar_store_get(name, &png, &len) == AVATAR_PACK_OK;
}

int avatar_store_first_name(char name[16]) {
    return avatar_pack_first_name(&s_pack, name);
}

int avatar_store_name_at(uint8_t index, char name[16]) {
    return avatar_pack_name_at(&s_pack, index, name);
}
