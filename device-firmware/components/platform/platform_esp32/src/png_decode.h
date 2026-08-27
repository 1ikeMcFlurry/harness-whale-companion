// components/platform/platform_esp32/src/png_decode.h —— 头像 PNG 流式解码(platform 内部)
#pragma once
#include <stdint.h>

// 解码 flash 中的 PNG → ARGB8888 整帧写入 imgframe(经 jpeg_frame_begin/write)。
// 支持:非隔行、8bit、颜色类型 6(RGBA)/2(RGB)/0(灰度)。RGB/灰度按不透明处理。
// 用 ROM 的 tinfl 流式解压(自备 32KB 循环字典 + ~11KB 解压器,堆上分配,含内存守卫)。
// 成功:填 *ow/*oh 返回 0。失败返回负:
//   -1 不是 PNG   -2 位深非8   -3 隔行不支持   -4 颜色类型不支持(如调色板)
//   -5 尺寸非法/超屏   -6 超 imgframe 容量(让 pad 出更小的图)
//   -7 内存不足(空闲堆不够解码,保留旧头像)   -8 flash 擦除失败   -9 解码流出错
int png_decode_to_frame(const uint8_t *png, int len, int *ow, int *oh);
