// components/platform/platform_esp32/src/identity_nvs.c —— hal_identity 的 NVS 实现
// 独立 cardid 分区,与系统 nvs 物理隔离:任何"恢复出厂设置"擦的是 nvs,碰不到身份。
#include "platform/platform_factory.h"
#include "hal/hal_identity.h"
#include "hal/hal_identity_provision.h"
#include "services/sn_format.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "mbedtls/md.h"
#include <string.h>

static const char *TAG = "identity";

// 身份 NVS 采用 FoloToy 产线工具的结构(见《FoloToy 主程序 ESP32-C3 烧写逻辑说明》):
//   namespace = folotoy-key;字段 DeviceKey/DeviceSecret/ProductKey/HardwareVersion/LampVersion。
// 固件内部概念 → FoloToy 字段映射:sn←DeviceKey, key←DeviceSecret, pk←ProductKey, hw←HardwareVersion。
// (LampVersion 灯板版本本固件不用,忽略。)
#define CARDID_PART "cardid"
#define CARDID_NS   "folotoy-key"
#define F_SN   "DeviceKey"        // 去分隔符的 MAC/SN
#define F_KEY  "DeviceSecret"     // 每设备密钥(绑定用)
#define F_PK   "ProductKey"       // 产品标识/密钥(HMAC 用)
#define F_HW   "HardwareVersion"  // 硬件版本(带 v 前缀)

#define SN_CAP   (SN_STR_LEN + 1)   // 13
#define KEY_CAP  65                 // DeviceSecret 可能是 32 位密钥,留足
#define PK_CAP   33                 // ProductKey(aliyun 风格可 ~20 字符)
#define HW_CAP   17

static char s_sn [SN_CAP];
static char s_key[KEY_CAP];
static char s_pk [PK_CAP];
static char s_hw [HW_CAP];
static bool s_ready;                 // 分区打开成功

// 读一个字符串键;不存在或为空 → dst[0]='\0'
static void load_str(nvs_handle_t h, const char *key, char *dst, size_t cap) {
    size_t sz = cap;
    dst[0] = '\0';
    esp_err_t e = nvs_get_str(h, key, dst, &sz);
    if (e == ESP_ERR_NVS_INVALID_LENGTH) {
        // 区别于"键不存在":存储值比固件的缓冲上限还长。不喊出来的话,
        // 现象与"没写过"完全一样,操作员会反复重烧同一个超长的 bin。
        ESP_LOGE(TAG, "NVS 键 '%s' 的值长度 %u 超过固件上限 %u,已按缺失处理",
                 key, (unsigned)sz, (unsigned)cap);
    }
    if (e != ESP_OK) dst[0] = '\0';
}

static int copy_out(const char *src, char *buf, int cap) {
    if (buf == NULL || cap <= 0) return -1;
    buf[0] = '\0';                      // 先满足"始终以 '\0' 结尾"的承诺,再判其它
    int n = (int)strlen(src);
    if (n == 0) return -1;
    if (n + 1 > cap) return -1;
    memcpy(buf, src, (size_t)n + 1);
    return n;
}

static int id_get_sn (hal_identity_t *s, char *b, int c) { (void)s; return copy_out(s_sn,  b, c); }
static int id_get_key(hal_identity_t *s, char *b, int c) { (void)s; return copy_out(s_key, b, c); }
static int id_get_hw (hal_identity_t *s, char *b, int c) { (void)s; return copy_out(s_hw,  b, c); }

static bool id_provisioned(hal_identity_t *s) {
    (void)s;
    return s_key[0] != '\0' && s_pk[0] != '\0';
}

// token 广播验签用 DeviceSecret(每设备密钥 s_key)做 HMAC —— 不用 ProductKey:
// FoloToy 的 ProductKey 是公开产品标识("folotoy"),拿它签谁都能伪造;每设备
// DeviceSecret 才是真正的秘密。网关须用该设备台账里的 DeviceSecret 逐台签名。
static int id_hmac(hal_identity_t *s, const uint8_t *msg, int len,
                   uint8_t *out, int out_cap) {
    (void)s;
    if (msg == NULL || len < 0 || out == NULL || out_cap <= 0 || out_cap > 32) return -1;
    if (s_key[0] == '\0') return -2;

    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) return -3;

    uint8_t full[32];
    int rc = mbedtls_md_hmac(info, (const uint8_t *)s_key, strlen(s_key),
                             msg, (size_t)len, full);
    if (rc != 0) return -4;
    memcpy(out, full, (size_t)out_cap);
    return 0;
}

static const hal_identity_api_t API = {
    .get_sn         = id_get_sn,
    .get_key        = id_get_key,
    .get_hw_ver     = id_get_hw,
    .is_provisioned = id_provisioned,
    .hmac           = id_hmac,
};
static hal_identity_t s_handle = { .api = &API, .impl = NULL };

// 局部更新:只写非 NULL 的字段。**不擦分区** —— 擦了会连 sn 一起没。
int hal_identity_set_fields(hal_identity_t *self,
                            const char *key, const char *pk, const char *hw) {
    (void)self;
    if (!s_ready) return -1;
    // 长度校验必须在这里做:NVS 能存下比固件缓冲更长的值,
    // 一旦写进去,内存副本被截断而 NVS 是全长,签名行为在重启前后会不一致。
    if ((key && strlen(key) >= KEY_CAP) ||
        (pk  && strlen(pk)  >= PK_CAP)  ||
        (hw  && strlen(hw)  >= HW_CAP)) {
        ESP_LOGE(TAG, "字段超长,拒绝写入 (上限 key=%d pk=%d hw=%d 字符)",
                 KEY_CAP - 1, PK_CAP - 1, HW_CAP - 1);
        return -1;
    }
    nvs_handle_t h;
    if (nvs_open_from_partition(CARDID_PART, CARDID_NS, NVS_READWRITE, &h) != ESP_OK) return -1;

    esp_err_t e = ESP_OK;
    if (key && e == ESP_OK) e = nvs_set_str(h, F_KEY, key);
    if (pk  && e == ESP_OK) e = nvs_set_str(h, F_PK,  pk);
    if (hw  && e == ESP_OK) e = nvs_set_str(h, F_HW,  hw);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) { ESP_LOGE(TAG, "写入失败: 0x%x", e); return -1; }

    // 同步内存副本
    if (key) { strncpy(s_key, key, KEY_CAP - 1); s_key[KEY_CAP - 1] = '\0'; }
    if (pk)  { strncpy(s_pk,  pk,  PK_CAP  - 1); s_pk [PK_CAP  - 1] = '\0'; }
    if (hw)  { strncpy(s_hw,  hw,  HW_CAP  - 1); s_hw [HW_CAP  - 1] = '\0'; }
    ESP_LOGI(TAG, "身份字段已更新 (key=%s pk=%s hw=%s)",
             key ? "改" : "-", pk ? "改" : "-", hw ? "改" : "-");
    return 0;
}

// ProductKey 的 SHA256 前 4 字节十六进制,供 AT+CARDID? 回读校验。
// 回显指纹而非明文:串口输出常被产线工装记录、上传 MES,明文密钥落日志即泄漏。
void identity_pk_fingerprint(char out[9]) {
    out[0] = '\0';
    if (s_pk[0] == '\0') { strcpy(out, "--------"); return; }
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t d[32];
    if (info == NULL || mbedtls_md(info, (const uint8_t *)s_pk, strlen(s_pk), d) != 0) {
        strcpy(out, "--------");
        return;
    }
    static const char HEX[] = "0123456789abcdef";
    for (int i = 0; i < 4; i++) {
        out[i * 2]     = HEX[(d[i] >> 4) & 0x0F];
        out[i * 2 + 1] = HEX[d[i] & 0x0F];
    }
    out[8] = '\0';
}

// cardid 四字段各自是否已写入(内存副本非空)。供产测逐字段检查存在性。
// 不回传 ProductKey 明文,只报在/不在。传 NULL 的项跳过。
// 注意:s_sn 恒由 MAC 回补,故 sn 基本总为 true;真正可能缺的是 key/pk/hw。
void identity_fields_present(bool *sn, bool *key, bool *pk, bool *hw) {
    if (sn)  *sn  = (s_sn[0]  != '\0');
    if (key) *key = (s_key[0] != '\0');
    if (pk)  *pk  = (s_pk[0]  != '\0');
    if (hw)  *hw  = (s_hw[0]  != '\0');
}

hal_identity_t *platform_create_identity(const board_config_t *cfg) {
    (void)cfg;

    // 先无条件把 SN 填成 MAC 派生值。设计约定"SN 恒有效",
    // 必须在任何可能提前 return 的 NVS 操作之前满足它。
    uint8_t mac[6];
    char mac_sn[SN_CAP];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    sn_from_mac(mac, mac_sn);
    memcpy(s_sn, mac_sn, sizeof mac_sn);

    esp_err_t e = nvs_flash_init_partition(CARDID_PART);
    if (e != ESP_OK) {
        // 刻意不调 nvs_flash_erase_partition:那是系统 nvs 分区的标准恢复模式,
        // 但 cardid 存的是产线一次性写入的身份,擦掉就永久没了。宁可降级运行,
        // 也不能让固件在用户看不见的地方销毁出厂数据。
        ESP_LOGE(TAG, "cardid 分区不可用 0x%x —— **未擦除**,身份功能降级运行。"
                      "请 dump 0x356000 排查后重新走产线烧录", e);
        return &s_handle;   // 句柄仍返回,SN 有效,key/pk/hw 为空 → is_provisioned()=false
    }
    s_ready = true;

    nvs_handle_t h;
    if (nvs_open_from_partition(CARDID_PART, CARDID_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "cardid 命名空间打开失败");
        return &s_handle;
    }
    char nvs_sn[SN_CAP];
    load_str(h, F_SN,  nvs_sn, SN_CAP);
    load_str(h, F_KEY, s_key, KEY_CAP);
    load_str(h, F_PK,  s_pk,  PK_CAP);
    load_str(h, F_HW,  s_hw,  HW_CAP);

    if (nvs_sn[0] == '\0') {
        // 自补:覆盖"分区没烧 / 产线漏烧 / bin 里 sn 字段为空"三种情况。
        // s_sn 已是 MAC 派生值,这里只需落盘。
        if (nvs_set_str(h, F_SN, s_sn) == ESP_OK && nvs_commit(h) == ESP_OK) {
            ESP_LOGW(TAG, "SN 缺失,已按 WIFI_STA MAC 自补: %s", s_sn);
        } else {
            ESP_LOGE(TAG, "SN 自补写入失败,本次开机仅内存有效: %s", s_sn);
        }
    } else {
        if (strcmp(nvs_sn, mac_sn) != 0) {
            // 只告警,不覆盖 —— NVS 里的 SN 是权威值,云端可能已按它建立绑定关系,
            // 固件擅自改写会让绑定凭空失效。
            // 这条专门拦"逐台生成的分区 bin 被烧到了另一台设备"这类产线事故:
            // 两台设备会有相同 SN,出厂测试完全看不出来,要等用户绑定冲突才暴露。
            ESP_LOGE(TAG, "SN(%s) 与本机 MAC(%s) 不符!可能是分区 bin 烧错设备,请核对台账",
                     nvs_sn, mac_sn);
        }
        memcpy(s_sn, nvs_sn, sizeof nvs_sn);   // NVS 值是权威值
    }
    nvs_close(h);

    ESP_LOGI(TAG, "身份载入: SN=%s hw=%s provisioned=%d",
             s_sn, s_hw[0] ? s_hw : "-", (int)id_provisioned(&s_handle));
    return &s_handle;
}
