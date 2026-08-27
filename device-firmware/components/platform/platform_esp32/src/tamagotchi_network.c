/*
 * Background Wi-Fi/SNTP service for the dedicated Tamagotchi image.
 *
 * Wi-Fi credentials live in NVS and are accepted only over the physical USB
 * serial connection. Passwords are never printed. Connection and NTP failures
 * merely schedule a retry; they never delay or stop the emulator task.
 */
#include "platform/tamagotchi_network.h"

#include "driver/usb_serial_jtag.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define WIFI_NAMESPACE       "tama_net"
#define WIFI_SSID_KEY        "ssid"
#define WIFI_PASSWORD_KEY    "password"
#define WIFI_CONNECTED_BIT   BIT0
#define WIFI_FAILED_BIT      BIT1
#define WIFI_CONNECT_TIMEOUT pdMS_TO_TICKS(30000)
#define SNTP_TIMEOUT         pdMS_TO_TICKS(12000)
#define CONSOLE_LINE_MAX     192

typedef enum {
    NET_NO_CONFIG,
    NET_IDLE,
    NET_CONNECTING,
    NET_SYNCING,
    NET_SYNCED,
    NET_RETRY_WAIT,
} net_state_t;

static const char *TAG = "tama_net";
static EventGroupHandle_t s_wifi_events;
static TaskHandle_t s_network_task;
static tamagotchi_time_sync_fn s_sync_cb;
static tamagotchi_game_time_fn s_game_time_cb;
static void *s_sync_user;
static volatile net_state_t s_state = NET_IDLE;
static volatile unsigned s_connect_retries;
static volatile bool s_attempting;
static volatile int64_t s_last_sync_epoch;
static bool s_wifi_initialized;
static esp_netif_t *s_sta_netif;

static const char *state_name(net_state_t state) {
    switch (state) {
        case NET_NO_CONFIG: return "no_config";
        case NET_IDLE: return "idle";
        case NET_CONNECTING: return "connecting";
        case NET_SYNCING: return "syncing";
        case NET_SYNCED: return "synced";
        case NET_RETRY_WAIT: return "retry_wait";
        default: return "unknown";
    }
}

static esp_err_t open_network_nvs(nvs_open_mode_t mode, nvs_handle_t *handle) {
    return nvs_open(WIFI_NAMESPACE, mode, handle);
}

static bool load_credentials(char ssid[33], char password[65]) {
    nvs_handle_t handle;
    if (open_network_nvs(NVS_READONLY, &handle) != ESP_OK) return false;
    size_t ssid_size = 33;
    size_t password_size = 65;
    esp_err_t ssid_error = nvs_get_str(handle, WIFI_SSID_KEY, ssid, &ssid_size);
    esp_err_t password_error = nvs_get_str(handle, WIFI_PASSWORD_KEY,
                                           password, &password_size);
    nvs_close(handle);
    if (ssid_error != ESP_OK || !ssid[0]) return false;
    if (password_error != ESP_OK) password[0] = '\0';
    return true;
}

static bool credentials_configured(void) {
    char ssid[33] = {0};
    char password[65] = {0};
    return load_credentials(ssid, password);
}

static bool save_credentials(const char *ssid, const char *password) {
    nvs_handle_t handle;
    if (open_network_nvs(NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t error = nvs_set_str(handle, WIFI_SSID_KEY, ssid);
    if (error == ESP_OK) error = nvs_set_str(handle, WIFI_PASSWORD_KEY, password);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error == ESP_OK;
}

static bool clear_credentials(void) {
    nvs_handle_t handle;
    esp_err_t error = open_network_nvs(NVS_READWRITE, &handle);
    if (error != ESP_OK) return error == ESP_ERR_NVS_NOT_FOUND;
    error = nvs_erase_all(handle);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error == ESP_OK;
}

static void network_event(void *argument, esp_event_base_t base,
                          int32_t event_id, void *event_data) {
    (void)argument;
    if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "IPv4 acquired: " IPSTR, IP2STR(&event->ip_info.ip));
        s_connect_retries = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        return;
    }
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        esp_netif_dhcp_status_t dhcp_status = ESP_NETIF_DHCP_INIT;
        esp_err_t error = esp_netif_dhcpc_get_status(s_sta_netif,
                                                     &dhcp_status);
        ESP_LOGI(TAG, "Wi-Fi link established; DHCP status=%d error=%s "
                      "heap=%u",
                 (int)dhcp_status, esp_err_to_name(error),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        return;
    }
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED &&
        s_attempting) {
        const wifi_event_sta_disconnected_t *event = event_data;
        ESP_LOGW(TAG, "Wi-Fi disconnected reason=%u",
                 event ? (unsigned)event->reason : 0u);
        if (++s_connect_retries < 3) {
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAILED_BIT);
        }
    }
}

static bool initialize_wifi(void) {
    if (s_wifi_initialized) return true;
    esp_err_t error = esp_netif_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return false;
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return false;
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) return false;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&config) != ESP_OK ||
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                   network_event, NULL) != ESP_OK ||
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                   network_event, NULL) != ESP_OK ||
        esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        return false;
    }
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    s_wifi_initialized = true;
    return true;
}

static bool connect_wifi(const char *ssid, const char *password) {
    wifi_config_t config = {0};
    snprintf((char *)config.sta.ssid, sizeof(config.sta.ssid), "%s", ssid);
    snprintf((char *)config.sta.password, sizeof(config.sta.password), "%s",
             password);
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);
    s_connect_retries = 0;
    if (esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK) return false;
    esp_err_t error = esp_wifi_start();
    if (error != ESP_OK && error != ESP_ERR_WIFI_CONN) return false;
    s_attempting = true;
    error = esp_wifi_connect();
    if (error != ESP_OK) {
        s_attempting = false;
        esp_wifi_stop();
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT, pdTRUE, pdFALSE,
        WIFI_CONNECT_TIMEOUT);
    if (!(bits & WIFI_CONNECTED_BIT)) {
        s_attempting = false;
        esp_wifi_disconnect();
        esp_wifi_stop();
        return false;
    }
    return true;
}

static bool synchronize_time(void) {
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("ntp.aliyun.com",
                                "time1.cloud.tencent.com",
                                "pool.ntp.org"));
    esp_err_t error = esp_netif_sntp_init(&config);
    if (error != ESP_OK) return false;
    error = esp_netif_sntp_sync_wait(SNTP_TIMEOUT);
    esp_netif_sntp_deinit();
    if (error != ESP_OK) return false;
    time_t now = time(NULL);
    if (now < 1577836800) return false; // 2020-01-01 sanity floor.
    s_last_sync_epoch = (int64_t)now;
    if (s_sync_cb) s_sync_cb((int64_t)now, s_sync_user);
    return true;
}

static void stop_wifi(void) {
    s_attempting = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
}

static void network_task(void *argument) {
    (void)argument;
    static const uint32_t retry_seconds[] = {30, 120, 600, 1800};
    unsigned retry_index = 0;
    for (;;) {
        char ssid[33] = {0};
        char password[65] = {0};
        if (!load_credentials(ssid, password)) {
            s_state = NET_NO_CONFIG;
            ESP_LOGI(TAG, "No Wi-Fi configured; game remains fully usable. "
                          "Use AT+WIFI=<ssid>,<password>");
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            retry_index = 0;
            continue;
        }
        if (!initialize_wifi()) {
            ESP_LOGE(TAG, "Wi-Fi initialization failed; retrying in background");
        } else {
            s_state = NET_CONNECTING;
            ESP_LOGI(TAG, "Connecting to Wi-Fi SSID '%s'", ssid);
            if (connect_wifi(ssid, password)) {
                s_state = NET_SYNCING;
                ESP_LOGI(TAG, "Wi-Fi connected; synchronizing time");
                if (synchronize_time()) {
                    s_state = NET_SYNCED;
                    ESP_LOGI(TAG, "Time synchronized epoch=%" PRId64,
                             s_last_sync_epoch);
                    stop_wifi();
                    retry_index = 0;
                    // Refresh once per hour while powered. A credential change
                    // wakes the wait immediately.
                    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3600u * 1000u));
                    continue;
                }
                ESP_LOGW(TAG, "NTP timed out; game continues with saved time");
                stop_wifi();
            } else {
                ESP_LOGW(TAG, "Wi-Fi unavailable; game continues offline");
            }
        }
        s_state = NET_RETRY_WAIT;
        uint32_t delay_seconds = retry_seconds[retry_index];
        if (retry_index + 1 < sizeof(retry_seconds) / sizeof(retry_seconds[0])) {
            retry_index++;
        }
        ESP_LOGI(TAG, "Next background time-sync attempt in %u seconds",
                 (unsigned)delay_seconds);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_seconds * 1000u));
    }
}

static bool starts_with_ci(const char *text, const char *prefix) {
    while (*prefix) {
        if (!*text) return false;
        if (toupper((unsigned char)*text++) != toupper((unsigned char)*prefix++)) {
            return false;
        }
    }
    return true;
}

static void notify_network_change(void) {
    if (s_wifi_initialized) stop_wifi();
    if (s_wifi_events) {
        xEventGroupSetBits(s_wifi_events, WIFI_FAILED_BIT);
    }
    if (s_network_task) xTaskNotifyGive(s_network_task);
}

static void handle_console_line(char *line) {
    while (*line == ' ' || *line == '\t') line++;
    char *end = line + strlen(line);
    while (end > line && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
    if (!*line) return;

    if (starts_with_ci(line, "AT+WIFI?")) {
        char ssid[33] = {0};
        char password[65] = {0};
        bool configured = load_credentials(ssid, password);
        printf("+WIFI: configured=%d,ssid=%s,state=%s\r\n",
               configured ? 1 : 0, configured ? ssid : "-",
               state_name(s_state));
    } else if (starts_with_ci(line, "AT+WIFI=CLEAR")) {
        if (clear_credentials()) {
            notify_network_change();
            printf("+OK wifi_cleared\r\n");
        } else {
            printf("+ERR=nvs_write\r\n");
        }
    } else if (starts_with_ci(line, "AT+WIFI=")) {
        char *value = line + strlen("AT+WIFI=");
        char *comma = strchr(value, ',');
        if (!comma) {
            printf("+ERR=arg(use AT+WIFI=<ssid>,<password>)\r\n");
        } else {
            *comma++ = '\0';
            size_t ssid_length = strlen(value);
            size_t password_length = strlen(comma);
            if (!ssid_length || ssid_length > 32 || password_length > 64) {
                printf("+ERR=arg(ssid 1..32,password 0..64 bytes)\r\n");
            } else if (save_credentials(value, comma)) {
                notify_network_change();
                printf("+OK wifi_saved,connecting_in_background\r\n");
            } else {
                printf("+ERR=nvs_write\r\n");
            }
        }
    } else if (starts_with_ci(line, "AT+TIME?")) {
        time_t now = time(NULL);
        time_t china_epoch = now + 8 * 60 * 60;
        struct tm china = {0};
        gmtime_r(&china_epoch, &china);
        uint8_t game_hour = 0;
        uint8_t game_minute = 0;
        uint8_t game_second = 0;
        bool game_valid = s_game_time_cb &&
            s_game_time_cb(&game_hour, &game_minute, &game_second,
                           s_sync_user);
        if (game_valid) {
            printf("+TIME: epoch=%" PRId64 ",local=%02d:%02d:%02d,"
                   "game=%02u:%02u:%02u,last_sync=%" PRId64 ",state=%s\r\n",
                   (int64_t)now, china.tm_hour, china.tm_min, china.tm_sec,
                   (unsigned)game_hour, (unsigned)game_minute,
                   (unsigned)game_second, s_last_sync_epoch,
                   state_name(s_state));
        } else {
            printf("+TIME: epoch=%" PRId64 ",local=%02d:%02d:%02d,"
                   "game=--:--:--,last_sync=%" PRId64 ",state=%s\r\n",
                   (int64_t)now, china.tm_hour, china.tm_min, china.tm_sec,
                   s_last_sync_epoch, state_name(s_state));
        }
    } else if (starts_with_ci(line, "AT+TIME=")) {
        char *tail = NULL;
        int64_t epoch = strtoll(line + strlen("AT+TIME="), &tail, 10);
        if (!tail || *tail || epoch < 1577836800LL || epoch > 4102444800LL) {
            printf("+ERR=arg(unix epoch 2020..2100)\r\n");
        } else {
            struct timeval value = {.tv_sec = (time_t)epoch, .tv_usec = 0};
            settimeofday(&value, NULL);
            s_last_sync_epoch = epoch;
            if (s_sync_cb) s_sync_cb(epoch, s_sync_user);
            printf("+OK time_set=%" PRId64 "\r\n", epoch);
        }
    } else if (starts_with_ci(line, "AT+HELP")) {
        printf("+HELP: AT+WIFI? | AT+WIFI=<ssid>,<password> | "
               "AT+WIFI=CLEAR | AT+TIME? | AT+TIME=<unix_epoch>\r\n");
    } else {
        printf("+ERR=unknown(use AT+HELP)\r\n");
    }
    fflush(stdout);
}

static void console_task(void *argument) {
    (void)argument;
    static char line[CONSOLE_LINE_MAX];
    unsigned used = 0;
    for (;;) {
        uint8_t byte;
        int count = usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(100));
        if (count <= 0) continue;
        if (byte == '\r' || byte == '\n') {
            if (used) {
                line[used] = '\0';
                handle_console_line(line);
                used = 0;
            }
        } else if (used + 1 < sizeof(line)) {
            line[used++] = (char)byte;
        } else {
            used = 0;
        }
    }
}

void tamagotchi_network_start(tamagotchi_time_sync_fn sync_cb,
                              tamagotchi_game_time_fn game_time_cb,
                              void *user) {
    s_sync_cb = sync_cb;
    s_game_time_cb = game_time_cb;
    s_sync_user = user;
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
        error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS metadata invalid; clearing only the NVS partition");
        if (nvs_flash_erase() == ESP_OK) error = nvs_flash_init();
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "NVS unavailable; Wi-Fi configuration cannot be stored");
        return;
    }
    s_wifi_events = xEventGroupCreate();
    if (!s_wifi_events) {
        ESP_LOGE(TAG, "Unable to allocate Wi-Fi event group");
        return;
    }
    usb_serial_jtag_driver_config_t usb_config =
        USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    error = usb_serial_jtag_driver_install(&usb_config);
    if (error == ESP_OK) {
        if (xTaskCreate(console_task, "tama_console", 3072, NULL, 3, NULL) !=
            pdPASS) {
            ESP_LOGW(TAG, "USB command task unavailable");
        } else {
            ESP_LOGI(TAG, "USB commands ready: AT+HELP");
        }
    } else {
        ESP_LOGW(TAG, "USB command driver unavailable: %s",
                 esp_err_to_name(error));
    }
    if (xTaskCreate(network_task, "tama_net", 5120, NULL, 2,
                    &s_network_task) != pdPASS) {
        ESP_LOGE(TAG, "Unable to start background network task");
        s_network_task = NULL;
        return;
    }
    if (!credentials_configured()) s_state = NET_NO_CONFIG;
}
