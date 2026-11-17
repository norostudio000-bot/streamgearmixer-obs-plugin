#define SGM_WORKER_TEST
#define SGM_SHM_NAME L"Local\\SGM-Worker-Test-V2"
#define SGM_LEGACY_NAME L"Local\\SGM-Worker-Test-V1"
#include "../sgm-audio-source.c"
#include <stdio.h>
int main(void)
{
    HANDLE map=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,(DWORD)SGM_MAPPING_BYTES,SGM_SHM_NAME);
    struct sgm_shm_header* h=MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,SGM_MAPPING_BYTES);
    if(!h)return 1;
    memset(h,0,SGM_MAPPING_BYTES);h->magic=SGM_MAGIC;h->version=2;h->channels=2;h->sample_rate=48000;
    h->ring_frames=SGM_RING_FRAMES;h->active=1;h->epoch=1;
    float* ring=(float*)((uint8_t*)h+SGM_DATA_OFFSET);
    for(uint32_t i=0;i<SGM_RING_FRAMES;++i){ring[i*2]=.125f;ring[i*2+1]=-.125f;}
    DWORD before,after;GetProcessHandleCount(GetCurrentProcess(),&before);
    int count=0;
    for(int cycle=0;cycle<30;++cycle)
    {
        obs_source_t sink={0};void* source=sgm_create(NULL,&sink);if(!source)return 2;
        for(int block=0;block<25;++block)
        {
            InterlockedIncrement64(&h->sequence);h->write_pos+=96;h->end_time_ns=os_gettime_ns();
            InterlockedIncrement64(&h->sequence);Sleep(2);
        }
        sgm_destroy(source);if(!sink.calls || sink.invalid)return 3;
        count+=sink.calls;LONG stopped=sink.calls;Sleep(2);if(sink.calls!=stopped)return 4;
    }
    GetProcessHandleCount(GetCurrentProcess(),&after);
    UnmapViewOfFile(h);CloseHandle(map);
    if(after>before+2)return 5;
    printf("PASS actual OBS worker: 30 create/destroy cycles, %d chunks, valid stereo/timestamps, handles %lu -> %lu\n",count,before,after);
    return 0;
}
