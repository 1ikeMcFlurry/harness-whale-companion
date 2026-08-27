// components/platform/platform_esp32/src/jpeg_store.h —— BLE 全屏图 imgstore 分区读写(platform 内部)
#pragma once
#include <stdint.h>
#include <stdbool.h>
// jpeg_slot_t 定义在对外头文件 platform_factory.h,
// 这里直接复用,避免两处重复定义。
#include "platform/platform_factory.h"

// begin 后 write/end 严格串行写入 imgstore。头像不经过这里。
int  jpeg_store_begin(jpeg_slot_t slot, uint32_t total_len);  // 0 / -1超长 / -2失败:擦区,清有效标记
int  jpeg_store_write(const uint8_t *d, int n);     // 0 / -2:顺序追加写
int  jpeg_store_end(void);                          // 0 / -2:写 header(magic+len)
bool jpeg_store_has_valid(jpeg_slot_t slot);        // header 有效且 len 合理
int  jpeg_store_mmap(jpeg_slot_t slot, const uint8_t **ptr, int *len); // 0:映射 data 区为只读指针
void jpeg_store_unmap(void);                        // 释放上次 mmap

// 解码后 RGB565 整帧存储(imgframe 分区),供 lv_image 从 flash 显示(不占整帧 RAM)
int  jpeg_frame_begin(uint32_t nbytes);             // 0 / -1超容量 / -2失败:擦 imgframe
int  jpeg_frame_write(uint32_t off, const uint8_t *d, int n);  // 写 imgframe 指定偏移
int  jpeg_frame_mmap(const uint8_t **ptr, uint32_t nbytes);    // 0:映射为只读指针
void jpeg_frame_unmap(void);                        // 释放
uint32_t jpeg_frame_capacity(void);                 // imgframe 分区容量(字节),0=找不到分区
