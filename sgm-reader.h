/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SGM_READER_H
#define SGM_READER_H
#include "sgm-protocol.h"
#include <string.h>
struct sgm_reader {
    int64_t position, epoch;
    uint32_t rate;
    uint64_t dropped, last_timestamp;
    int ready;
};
/* Uses the SAME decoder in the plugin and the regression harness. Caller maps
 * SGM_MAPPING_BYTES and provides capacity*2 floats. No allocation or waiting. */
static int sgm_read_chunk(struct sgm_reader* r,const volatile struct sgm_shm_header* h,
                         const volatile float* ring,uint32_t version,uint64_t now,
                         float* dst,uint32_t capacity,uint32_t* frames,uint64_t* timestamp)
{
    for(int attempt=0;attempt<3;++attempt) {
        LONG64 sequence=version==2 ? h->sequence : 0;
        MemoryBarrier();
        if(sequence&1) continue;
        if(!sgm_valid_header(h,version)) return -1;
        uint32_t sr=h->sample_rate;
        LONG64 epoch=version==2 ? h->epoch : 0;
        LONG64 w=h->write_pos;
        uint64_t end=version==2 ? (uint64_t)h->end_time_ns : now;
        uint32_t active=version==2 ? h->active : 1;
        MemoryBarrier();
        if(version==2 && h->sequence!=sequence) continue;
        if(!active || !end || w<0) return 0;
        if(!r->ready || r->epoch!=epoch || r->rate!=sr || w<r->position) {
            r->position=w;r->epoch=epoch;r->rate=sr;r->ready=1;
            return 0; /* attach/restart at live, never replay previous session */
        }
        int64_t position=r->position;
        int64_t available=w-position;
        if(available<=0) return 0;
        /* The old ring/4 recovery retained 683 ms at 48k. Bound stale audio to
         * 10 ms after a 100-ms scheduling stall, independent of ring capacity. */
        if(available>(int64_t)(sr/10)) position=w-(int64_t)(sr/100);
        available=w-position;
        uint32_t n=available>(int64_t)capacity ? capacity : (uint32_t)available;
        if(!n) return 0;
        uint32_t at=(uint32_t)position&(SGM_RING_FRAMES-1);
        uint32_t first=SGM_RING_FRAMES-at;
        if(first>n) first=n;
        memcpy(dst,(const void*)(ring+(size_t)at*2),(size_t)first*2*sizeof(float));
        if(n>first) memcpy(dst+first*2,(const void*)ring,(size_t)(n-first)*2*sizeof(float));
        MemoryBarrier();
        if(version==2 && h->sequence!=sequence) continue;
        if(version==1 && h->write_pos-position>(LONG64)SGM_RING_FRAMES) continue;
        const uint64_t age=sgm_frames_ns((uint64_t)(w-position),sr);
        uint64_t ts=end>age ? end-age : 0;
        if(!ts) return 0;
        /* Frame position, not the current chunk length, determines its time.
         * Even a multi-chunk drain has correctly ordered contiguous timestamps. */
        if(ts<r->last_timestamp) ts=r->last_timestamp;
        r->dropped+=(uint64_t)(position-r->position);
        r->position=position+n;
        r->last_timestamp=ts;
        *frames=n;*timestamp=ts;
        return 1;
    }
    return 0; /* writer busy: retry next tick, never deliver a torn chunk */
}
#endif
