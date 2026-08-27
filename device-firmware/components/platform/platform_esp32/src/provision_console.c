// components/platform/platform_esp32/src/provision_console.c
// USB-Serial-JTAG 上的产线/返修指令。主产线路径是烧录 cardid 分区 bin,
// 这里只用于返修补写与现场排查。
//
// 不引入 esp_console:它的 REPL 会带来 linenoise 缓冲 + 补全/历史表 + 4KB 栈任务,
// 而这块板子的静态 RAM 余量直接决定 NimBLE 能否初始化成功(有过大块静态缓冲
// 压垮 BLE 堆的事故)。3 条固定格式指令不需要那套框架。
#include "platform/platform_factory.h"
#include "hal/hal_identity.h"
#include "hal/hal_identity_provision.h"
#include "services/prov_cmd.h"
#include "services/factory_test.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "prov";

#define PROV_LINE_MAX 160

static hal_identity_t   *s_id;
static identity_changed_fn s_changed_cb;
static void                *s_changed_user;
static factory_test_fn     s_test_cb;
static void                *s_test_user;
static at_cmd_fn           s_at_cb;
static void                *s_at_user;

void provision_console_set_changed_cb(identity_changed_fn cb, void *user) {
    s_changed_cb = cb; s_changed_user = user;
}

void provision_console_set_test_cb(factory_test_fn cb, void *user) {
    s_test_cb = cb; s_test_user = user;
}

void provision_console_set_at_cb(at_cmd_fn cb, void *user) {
    s_at_cb = cb; s_at_user = user;
}

// 大小写不敏感前缀匹配(跳过前导空白);命中返回前缀后的指针,否则 NULL。
static const char *match_ci(const char *line, int len, const char *pfx) {
    int i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    int pl = (int)strlen(pfx);
    if (len - i < pl) return NULL;
    for (int k = 0; k < pl; k++) {
        char a = line[i + k], b = pfx[k];
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        if (a != b) return NULL;
    }
    return line + i + pl;
}

// AT+TEST 系列报告可能多行(AUTO),给足缓冲。栈上分配,console_task 栈已放宽到 4096。
#define PROV_TEST_OUT 512

static void handle_line(const char *line, int len) {
    if (s_id == NULL) { printf("+ERR=no_identity\r\n"); return; }

    // FoloToy 风格分组指令:at+config / at+command / at+reboot → 交 app 的 at 处理器。
    // (AT+CARDID / AT+TEST 在第 4 字符即与 CONFIG/COMMAND 分岔,不会误命中。)
    if (s_at_cb && (match_ci(line, len, "AT+CONFIG") ||
                    match_ci(line, len, "AT+COMMAND") ||
                    match_ci(line, len, "AT+REBOOT"))) {
        static char out[256];
        int wl = s_at_cb(line, len, out, sizeof out, s_at_user);
        if (wl > 0) { fwrite(out, 1, (size_t)wl, stdout); fflush(stdout); }
        return;
    }

    // 先看是不是产测命令(仅认 AT+TEST 前缀),是则交产测回调,不落 AT+CARDID 解析。
    ft_cmd_t t = ft_parse(line, len);
    if (t != FT_NONE) {
        if (!s_test_cb) { printf("+ERR=no_test\r\n"); return; }
        static char out[PROV_TEST_OUT];   // static:不压栈;串口任务单线程,无重入
        int wl = s_test_cb(t, out, sizeof out, s_test_user);
        if (wl > 0) fwrite(out, 1, (size_t)wl, stdout);
        fflush(stdout);
        return;
    }

    prov_cmd_t c;
    prov_parse(line, len, &c);

    if (c.kind == PROV_CMD_NONE && c.err == PROV_ERR_NONE) return;   // 空行,静默

    if (c.err != PROV_ERR_NONE) {
        printf("+ERR=%s\r\n", prov_err_str(c.err));
        return;
    }

    switch (c.kind) {
        case PROV_CMD_SET: {
            int rc = hal_identity_set_fields(s_id,
                                             c.has_key ? c.key : NULL,
                                             c.has_pk  ? c.pk  : NULL,
                                             c.has_hw  ? c.hw  : NULL);
            // 通知 app 重新推送 BLE 身份 blob,否则 …0012 特征会一直吐旧值
            if (rc == 0 && s_changed_cb) s_changed_cb(s_changed_user);
            printf(rc == 0 ? "+OK\r\n" : "+ERR=nvs_write\r\n");
            break;
        }
        case PROV_CMD_QUERY: {
            char sn[16] = {0}, hw[16] = {0}, fp[9];
            hal_identity_get_sn(s_id, sn, sizeof sn);
            hal_identity_get_hw_ver(s_id, hw, sizeof hw);
            identity_pk_fingerprint(fp);
            // 刻意不回显 key 与 pk 明文,只给 pk 指纹。
            printf("+CARDID: sn=%s,hw=%s,pk_fp=%s,provisioned=%d\r\n",
                   sn[0] ? sn : "-", hw[0] ? hw : "-", fp,
                   hal_identity_is_provisioned(s_id) ? 1 : 0);
            break;
        }
        default:
            printf("+ERR=unknown\r\n");
            break;
    }
}

static void console_task(void *arg) {
    (void)arg;
    static char line[PROV_LINE_MAX];
    int have = 0;
    for (;;) {
        uint8_t ch;
        int n = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(100));
        if (n <= 0) continue;
        if (ch == '\n' || ch == '\r') {
            if (have > 0) { handle_line(line, have); have = 0; }
            continue;
        }
        if (have < PROV_LINE_MAX - 1) line[have++] = (char)ch;
        else have = 0;   // 超长行整条丢弃,避免半截指令被当成完整指令解析
    }
}

void provision_console_start(hal_identity_t *id) {
    s_id = id;
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t e = usb_serial_jtag_driver_install(&cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "USB-Serial-JTAG 驱动安装失败 0x%x,产线指令不可用", e);
        return;
    }
    // 栈 4096:产测回调(AT+TEST=AUDIO)会走 esp_codec_dev/i2s 调用链,比纯写 NVS 深。
    if (xTaskCreate(console_task, "prov", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "指令任务创建失败(堆不足),产线指令不可用");
        return;
    }
    ESP_LOGI(TAG, "产线指令就绪: AT+CARDID=/? / AT+TEST? / at+config=? / at+command=? / at+reboot");
}
