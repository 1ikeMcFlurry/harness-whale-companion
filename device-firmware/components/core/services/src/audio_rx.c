// components/core/services/src/audio_rx.c —— 0x05 片段接收 + 0x04 流 BEGIN 解析(纯逻辑)
#include "services/audio_rx.h"
#include "services/audio_clip.h"

void audio_clip_rx_init(audio_clip_rx_t *r){ r->receiving=0; r->clip_id=-1; r->total=0; r->written=0; }

int audio_clip_rx_frame(audio_clip_rx_t *r, const uint8_t *payload, int len,
                        const audio_clip_sink_t *sink, int *done){
    if(done) *done=0;
    if(len<1) return 1;
    uint8_t op=payload[0]; const uint8_t *b=payload+1; int bl=len-1;
    if(op==AUDIO_OP_BEGIN){
        if(bl<5) return 1;                            // clip_id(1) + total_len(4, u32 小端)
        int id=b[0];
        uint32_t total=(uint32_t)b[1]|((uint32_t)b[2]<<8)|((uint32_t)b[3]<<16)|((uint32_t)b[4]<<24);
        int rc=sink->begin(sink->user, id, total);
        if(rc!=0){ r->receiving=0; return 3; }   // clip_id 非法/超长/打开失败一律 3
        r->receiving=1; r->clip_id=id; r->total=total; r->written=0;
        return 0;
    }
    if(op==AUDIO_OP_DATA){
        if(!r->receiving) return 1;
        if(bl>0){
            if(r->written+(uint32_t)bl > r->total){ r->receiving=0; return 3; }
            if(sink->write(sink->user,b,bl)!=0){ r->receiving=0; return 2; }
            r->written+=(uint32_t)bl;
        }
        return 0;
    }
    if(op==AUDIO_OP_END){
        if(!r->receiving) return 1;
        r->receiving=0;
        if(r->written!=r->total) return 1;
        if(sink->end(sink->user)!=0) return 2;
        if(done) *done=1;
        return 0;
    }
    return 1;
}
