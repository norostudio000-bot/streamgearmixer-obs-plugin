/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Noro. See LICENSE for GPL v2 or later terms.
 * Timed, low-backlog shared-memory source for OBS Studio.
 */
#include <windows.h>
#include <avrt.h>
#ifdef SGM_WORKER_TEST
#include "Tests/obs-test-stub.h"
#else
#include <obs-module.h>
#include <util/platform.h>
#endif
#include "sgm-reader.h"
OBS_DECLARE_MODULE()
#define SGM_CHUNK_FRAMES 1024u
struct sgm_source {
    obs_source_t* source;
    HANDLE mapping,thread,stop_event,timer,producer;
    const volatile struct sgm_shm_header* header;
    const volatile float* ring;
    uint32_t version;
    struct sgm_reader reader;
    float chunk[SGM_CHUNK_FRAMES*2];
};
static void sgm_detach(struct sgm_source* s)
{
    if(s->header) UnmapViewOfFile((const void*)s->header);
    if(s->mapping) CloseHandle(s->mapping);
    if(s->producer) CloseHandle(s->producer);
    s->producer=NULL;
    s->header=NULL;s->ring=NULL;s->mapping=NULL;
    memset(&s->reader,0,sizeof(s->reader));
}
static bool sgm_try_attach(struct sgm_source* s)
{
    for(uint32_t version=2;version>=1;--version) {
        HANDLE mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,version==2?SGM_SHM_NAME:SGM_LEGACY_NAME);
        if(!mapping) continue;
        const volatile struct sgm_shm_header* h=MapViewOfFile(mapping,FILE_MAP_READ,0,0,SGM_MAPPING_BYTES);
        if(!h) {CloseHandle(mapping);continue;}
        if(!sgm_valid_header(h,version)) {UnmapViewOfFile((const void*)h);CloseHandle(mapping);continue;}
        HANDLE producer=version==2 && h->producer_pid ? OpenProcess(SYNCHRONIZE,FALSE,h->producer_pid) : NULL;
        if(version==2 && h->producer_pid && (!producer || WaitForSingleObject(producer,0)==WAIT_OBJECT_0)) {
            if(producer)CloseHandle(producer);
            UnmapViewOfFile((const void*)h);CloseHandle(mapping);continue;
        }
        s->producer=producer;
        s->mapping=mapping;s->header=h;s->version=version;
        s->ring=(const volatile float*)((const uint8_t*)h+SGM_DATA_OFFSET);
        memset(&s->reader,0,sizeof(s->reader));
        blog(LOG_INFO,"[sgm-audio-source] attached V%u (%u Hz); timed sync build 2026-09-07",version,h->sample_rate);
        return true;
    }
    return false;
}
static DWORD WINAPI sgm_thread(LPVOID param)
{
    struct sgm_source* s=param;
    DWORD task_index=0;
    HANDLE mmcss=AvSetMmThreadCharacteristicsW(L"Audio",&task_index);
    uint64_t next_attach=0,next_upgrade=0;
    HANDLE waits[2]={s->stop_event,s->timer};
    for(;;) {
        if(WaitForSingleObject(s->stop_event,0)==WAIT_OBJECT_0) break;
        const uint64_t now=os_gettime_ns();
        if(s->producer && WaitForSingleObject(s->producer,0)==WAIT_OBJECT_0)sgm_detach(s);
        if(!s->header && now>=next_attach) {
            next_attach=now+250000000ULL;
            sgm_try_attach(s);
        }
        /* An old mixer may be closed and a V2 mixer opened while OBS stays up. */
        if(s->header && s->version==1 && now>=next_upgrade) {
            next_upgrade=now+1000000000ULL;
            HANDLE newer=OpenFileMappingW(FILE_MAP_READ,FALSE,SGM_SHM_NAME);
            if(newer) {CloseHandle(newer);sgm_detach(s);sgm_try_attach(s);}
        }
        if(s->header) {
            for(int batch=0;batch<32;++batch) {
                if(WaitForSingleObject(s->stop_event,0)==WAIT_OBJECT_0) break;
                uint32_t n=0;uint64_t timestamp=0;
                int result=sgm_read_chunk(&s->reader,s->header,s->ring,s->version,
                                          now,s->chunk,SGM_CHUNK_FRAMES,&n,&timestamp);
                if(result<0) {sgm_detach(s);break;}
                if(!result) break;
                struct obs_source_audio audio={0};
                audio.data[0]=(const uint8_t*)s->chunk;audio.frames=n;
                audio.speakers=SPEAKERS_STEREO;audio.format=AUDIO_FORMAT_FLOAT;
                audio.samples_per_sec=s->reader.rate;audio.timestamp=timestamp;
                obs_source_output_audio(s->source,&audio);
            }
        }
        if(s->timer) {
            if(WaitForMultipleObjects(2,waits,FALSE,100)==WAIT_OBJECT_0) break;
        } else if(WaitForSingleObject(s->stop_event,2)==WAIT_OBJECT_0) break;
    }
    if(mmcss) AvRevertMmThreadCharacteristics(mmcss);
    return 0;
}
static const char* sgm_get_name(void* unused) {UNUSED_PARAMETER(unused);return "StreamGearMixer";}
static void sgm_destroy(void* data)
{
    struct sgm_source* s=data;
    if(!s) return;
    if(s->stop_event) SetEvent(s->stop_event);
    /* Never free state still owned by a worker after an arbitrary timeout. */
    if(s->thread) {WaitForSingleObject(s->thread,INFINITE);CloseHandle(s->thread);}
    if(s->timer) {CancelWaitableTimer(s->timer);CloseHandle(s->timer);}
    if(s->stop_event) CloseHandle(s->stop_event);
    sgm_detach(s);bfree(s);
}
static void* sgm_create(obs_data_t* settings,obs_source_t* source)
{
    UNUSED_PARAMETER(settings);
    struct sgm_source* s=bzalloc(sizeof(*s));
    if(!s) return NULL;
    s->source=source;s->stop_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!s->stop_event) {sgm_destroy(s);return NULL;}
    s->timer=CreateWaitableTimerExW(NULL,NULL,0x00000002,TIMER_ALL_ACCESS);
    if(!s->timer) s->timer=CreateWaitableTimerW(NULL,FALSE,NULL);
    if(s->timer) {
        LARGE_INTEGER due;due.QuadPart=-10000;
        if(!SetWaitableTimer(s->timer,&due,1,NULL,NULL,FALSE)) {CloseHandle(s->timer);s->timer=NULL;}
    }
    s->thread=CreateThread(NULL,0,sgm_thread,s,0,NULL);
    if(!s->thread) {sgm_destroy(s);return NULL;}
    return s;
}
bool obs_module_load(void)
{
    struct obs_source_info info={0};
    info.id="sgm_stream_audio_source";info.type=OBS_SOURCE_TYPE_INPUT;
    info.output_flags=OBS_SOURCE_AUDIO;info.get_name=sgm_get_name;
    info.create=sgm_create;info.destroy=sgm_destroy;info.icon_type=OBS_ICON_TYPE_AUDIO_OUTPUT;
    obs_register_source(&info);
    blog(LOG_INFO,"[sgm-audio-source] timed sync build 2026-09-07 loaded (V2 + V1 fallback)");
    return true;
}
MODULE_EXPORT const char* obs_module_description(void){return "StreamGearMixer timed audio link (V2, low backlog)";}
MODULE_EXPORT const char* obs_module_name(void){return "StreamGearMixer Audio Source";}
