/* SPDX-License-Identifier: MIT
 * StreamGearMixer shared-memory interface. Standalone C/C++ wire definition.
 * Copyright (c) 2026 Noro. Permission is granted to use, copy, modify and
 * distribute this file, with this notice, without warranty of any kind.
 */
#ifndef SGM_PROTOCOL_H
#define SGM_PROTOCOL_H
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#ifndef SGM_SHM_NAME
#define SGM_SHM_NAME L"Local\\StreamGearMixerAudioV2"
#endif
#ifndef SGM_LEGACY_NAME
#define SGM_LEGACY_NAME L"Local\\StreamGearMixerAudioV1"
#endif
#define SGM_MAGIC 0x53474D58u
#define SGM_VERSION 2u
#define SGM_RING_FRAMES 131072u
#define SGM_DATA_OFFSET 4096u
#define SGM_MAPPING_BYTES (SGM_DATA_OFFSET + SGM_RING_FRAMES * 2u * sizeof(float))
#pragma pack(push,8)
struct sgm_shm_header {
    uint32_t magic, version, sample_rate, channels, ring_frames;
    uint32_t active;
    volatile LONG64 sequence;       /* odd while writer touches metadata/audio */
    volatile LONG64 end_time_ns;    /* QPC clock, same epoch as OBS os_gettime_ns */
    volatile LONG64 epoch;          /* changes on restart / device rate change */
    uint32_t producer_pid, block_frames;
    volatile LONG64 write_pos;
};
#pragma pack(pop)
typedef char sgm_header_size_check[sizeof(struct sgm_shm_header)==64 ? 1 : -1];
typedef char sgm_write_alignment_check[offsetof(struct sgm_shm_header,write_pos)==56 ? 1 : -1];
static inline uint64_t sgm_frames_ns(uint64_t frames,uint32_t rate)
{
    return frames/rate*1000000000ULL + frames%rate*1000000000ULL/rate;
}
static inline uint64_t sgm_clock_ns(uint64_t frequency)
{
    LARGE_INTEGER ticks; QueryPerformanceCounter(&ticks);
    return (uint64_t)ticks.QuadPart/frequency*1000000000ULL
        + (uint64_t)ticks.QuadPart%frequency*1000000000ULL/frequency;
}
static inline int sgm_valid_header(const volatile struct sgm_shm_header* h,uint32_t version)
{
    return h->magic==SGM_MAGIC && h->version==version && h->channels==2
        && h->ring_frames==SGM_RING_FRAMES && h->sample_rate>=8000 && h->sample_rate<=384000;
}
#endif
