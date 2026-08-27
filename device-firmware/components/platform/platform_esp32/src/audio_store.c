// components/platform/platform_esp32/src/audio_store.c —— 片段存 SPIFFS(可写,原子替换)
#include "platform/platform_factory.h"
#include "platform/board_config.h"
#include "services/audio_clip.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "audio_store";
static int  s_mounted = 0;
// 片段存在性缓存(RAM):init 扫描 + 写成功时更新。audio_store_clip_exists 只读它,
// 不碰 SPIFFS —— 这样可被 NimBLE host 任务(连接回调)安全调用,不阻塞协议栈。
static bool s_present[CLIP_COUNT];

static void clip_path(int id, char *out, int cap){ snprintf(out,cap,"%s/clip%d.adpcm",AUDIO_SPIFFS_MOUNT,id); }
static void tmp_path (int id, char *out, int cap){ snprintf(out,cap,"%s/clip%d.tmp",  AUDIO_SPIFFS_MOUNT,id); }

// 磁盘校验单个片段头(仅 init 扫描用,不在热路径)。
static bool clip_valid_on_disk(int id){
    char p[48]; clip_path(id,p,sizeof p);
    FILE *f=fopen(p,"rb"); if(!f) return false;
    audio_clip_hdr_t h; size_t n=fread(&h,1,sizeof h,f); fclose(f);
    return (n==sizeof h && h.magic==AUDIO_CLIP_MAGIC);
}

int audio_store_init(void){
    if(s_mounted) return 0;
    esp_vfs_spiffs_conf_t c = {
        .base_path = AUDIO_SPIFFS_MOUNT, .partition_label = AUDIO_SPIFFS_LABEL,
        .max_files = 4, .format_if_mount_failed = true,   // 读+写+扫描可能并发多个句柄
    };
    esp_err_t e = esp_vfs_spiffs_register(&c);
    if(e != ESP_OK){ ESP_LOGW(TAG,"SPIFFS 挂载失败: %s", esp_err_to_name(e)); return -1; }
    s_mounted = 1;
    for(int i=0;i<CLIP_COUNT;i++) s_present[i]=clip_valid_on_disk(i);   // 建存在性缓存
    ESP_LOGI(TAG,"audio 分区已挂载 %s (present: %d/%d/%d)",
             AUDIO_SPIFFS_MOUNT, s_present[0], s_present[1], s_present[2]);
    return 0;
}

// 纯 RAM 读,任意任务安全(含 NimBLE host)。
bool audio_store_clip_exists(int clip_id){
    if(!s_mounted || clip_id<0 || clip_id>=CLIP_COUNT) return false;
    return s_present[clip_id];
}

// ---- 流式读:打开(跳过 12B 头 + 校验 magic)→ 逐块读 → 关闭。播放任务用,不整片进 RAM ----
static FILE *s_rd_fp = NULL;
int audio_store_clip_open(int clip_id){
    if(!s_mounted || clip_id<0 || clip_id>=CLIP_COUNT) return -1;
    if(s_rd_fp){ fclose(s_rd_fp); s_rd_fp=NULL; }
    char p[48]; clip_path(clip_id,p,sizeof p);
    FILE *f=fopen(p,"rb"); if(!f) return -1;
    audio_clip_hdr_t h;
    if(fread(&h,1,sizeof h,f)!=sizeof h || h.magic!=AUDIO_CLIP_MAGIC){ fclose(f); return -1; }
    s_rd_fp=f;   // 文件位置已停在头之后,后续读到的都是 ADPCM
    return 0;
}
int audio_store_clip_read_chunk(uint8_t *buf, int max){
    if(!s_rd_fp || max<=0) return 0;
    return (int)fread(buf,1,(size_t)max,s_rd_fp);
}
void audio_store_clip_close(void){ if(s_rd_fp){ fclose(s_rd_fp); s_rd_fp=NULL; } }

// ---- sink:写临时文件,end 校验头 + 原子 rename ----
static int s_id=-1; static FILE *s_fp=NULL; static uint32_t s_first4=0; static int s_hdrbytes=0;
static int sink_begin(void *u,int id,uint32_t total){
    (void)u;(void)total;
    if(!s_mounted || id<0 || id>=CLIP_COUNT) return -1;
    char p[48]; tmp_path(id,p,sizeof p);
    if(s_fp){ fclose(s_fp); s_fp=NULL; }
    s_fp=fopen(p,"wb"); if(!s_fp) return -2;
    s_id=id; s_hdrbytes=0; s_first4=0; return 0;
}
static int sink_write(void *u,const uint8_t *d,int n){
    (void)u; if(!s_fp) return -2;
    // 逐字节累积前 4 字节(magic)供 end 校验 —— 不假设首个 DATA 帧就 ≥4 字节(可能被分小片)。
    for(int i=0; i<n && s_hdrbytes<4; i++) ((uint8_t*)&s_first4)[s_hdrbytes++]=d[i];
    return (fwrite(d,1,(size_t)n,s_fp)==(size_t)n)?0:-2;
}
static int sink_end(void *u){
    (void)u; if(!s_fp) return -2;
    fflush(s_fp); fclose(s_fp); s_fp=NULL;
    if(s_hdrbytes<4 || s_first4!=AUDIO_CLIP_MAGIC){       // 头非法 → 删临时,不替换
        char t[48]; tmp_path(s_id,t,sizeof t); remove(t); return -2;
    }
    char t[48],p[48]; tmp_path(s_id,t,sizeof t); clip_path(s_id,p,sizeof p);
    remove(p);                                            // SPIFFS rename 不覆盖已存在,先删
    if(rename(t,p)!=0) return -2;
    if(s_id>=0 && s_id<CLIP_COUNT) s_present[s_id]=true;  // 替换成功 → 更新存在性缓存
    return 0;
}
static const audio_clip_sink_t SINK = { sink_begin, sink_write, sink_end, NULL };
const audio_clip_sink_t *audio_store_sink(void){ return &SINK; }
