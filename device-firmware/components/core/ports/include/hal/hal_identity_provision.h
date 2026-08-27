// components/core/ports/include/hal/hal_identity_provision.h —— 设备身份(写入)
//
// 刻意与 hal_identity.h 分开:写入是"物理接触即授权"的高权限操作。
// BLE 层只 include hal_identity.h,于是在**编译期就看不见**写入函数,
// 误用需要主动加 include,会在 review 中显眼地暴露出来。
//
// 只有 provision_console.c 与 identity_nvs.c 自身应该 include 本文件。
#pragma once
#include "hal/hal_identity.h"

// 局部更新:仅非 NULL 的字段写入 NVS,其余键原样保留。**不擦分区**
// (擦分区会连 sn 一起抹掉)。成功返回 0,失败返回 <0。
// 没有 sn 参数 —— SN 由 eFuse MAC 决定,运行时不可改。
int hal_identity_set_fields(hal_identity_t *self,
                            const char *key, const char *pk, const char *hw);
