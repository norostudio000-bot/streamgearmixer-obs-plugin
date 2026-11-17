#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "../sgm-protocol.h"
#define OBS_DECLARE_MODULE()
#define UNUSED_PARAMETER(x) (void)(x)
#define MODULE_EXPORT
#define LOG_INFO 1
#define SPEAKERS_STEREO 2
#define AUDIO_FORMAT_FLOAT 1
#define OBS_SOURCE_TYPE_INPUT 1
#define OBS_SOURCE_AUDIO 1
#define OBS_ICON_TYPE_AUDIO_OUTPUT 1
typedef struct {volatile LONG calls;volatile LONG invalid;uint64_t last;} obs_source_t;
typedef void obs_data_t;
struct obs_source_audio {const uint8_t* data[8];uint32_t frames,speakers,format,samples_per_sec;uint64_t timestamp;};
struct obs_source_info {const char* id;int type,output_flags;const char*(*get_name)(void*);void*(*create)(obs_data_t*,obs_source_t*);void(*destroy)(void*);int icon_type;};
static uint64_t os_gettime_ns(void){LARGE_INTEGER f;QueryPerformanceFrequency(&f);return sgm_clock_ns(f.QuadPart);}
static void* bzalloc(size_t n){return calloc(1,n);}
static void bfree(void* p){free(p);}
static void blog(int level,const char* format,...){(void)level;(void)format;}
static void obs_register_source(const struct obs_source_info* info){(void)info;}
static void obs_source_output_audio(obs_source_t* s,const struct obs_source_audio* a)
{
    const float* p=(const float*)a->data[0];
    if(a->timestamp<s->last || a->samples_per_sec!=48000 || a->speakers!=2)InterlockedIncrement(&s->invalid);
    for(uint32_t i=0;i<a->frames;++i)if(p[i*2]!=.125f || p[i*2+1]!=-.125f)InterlockedIncrement(&s->invalid);
    s->last=a->timestamp;InterlockedIncrement(&s->calls);
}
