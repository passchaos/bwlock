#include "common.h"

// 采样时间间隔
extern int g_period_us;
#define CACHE_LINE_SIZE 64


u64 convert_mb_to_events(u64 mb)
{
    return div64_u64(mb * 1024 * 1024, 64);
    // return mb * 1024 * 1024 / 1000 / 64;
    // u64 data_per_sec = mb * 1024 * 1024;
    // u64 total_bytes = data_per_sec * g_period_us / 1000000;
    // return total_bytes / CACHE_LINE_SIZE;
}

u64 convert_events_to_mb(u64 events)
{
    u64 total_bytes = events * CACHE_LINE_SIZE;
    u64 mb = div64_u64(total_bytes * 1000000, g_period_us * 1024 * 1024);
    return mb;
}
