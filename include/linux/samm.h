/**
 * @file samm.h
 * @author hwp (hwpark@dgist.ac.kr)
 * @brief definition for "spatial-locality aware memory management".
 * @version 0.1
 * @date 2023-12-04
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#include <uapi/linux/perf_event.h>

// about SAMM
#ifdef CONFIG_SAMM

#define SAMM_SEQ_THRESHOLD                  8

#endif /* CONFIG_SAMM */

// about 
#ifdef CONFIG_FREQ_PROMOTION

#define MAX_GLOBAL_FREQ_BUF                 2

#define NUM_CPU_ON_SOCKET                   8 /* number of cpu in one numa socket */

#define PEBS_BUF_SIZE                       32 /* 0.25 MB, should be adjust later */
#define PEBS_SLEEP_DURATION                 1000
#define PEBS_SAMPLE_PERIOD                  1024

struct samm_event{
    struct perf_event_header header;
    __u64 ip;
    __u32 pid, tid;
    __u64 addr;
    __u64 phys_addr;
};

// PEBS events in skylake
#define MEM_LOAD_L3_HIT                     0x1d2
#define MEM_LOAD_L3_MISS_LOCAL              0x1d3
#define MEM_LOAD_L3_MISS_REMOTE             0x2d3
#define MEM_LOAD_RETIRED_L3_HIT             0x4d1
#define MEM_LOAD_RETIRED_L3_MISS            0x20d1
#define MEM_INST_RETIRED_STLB_MISS_LOAD     0x11d0
#define MEM_INST_RETIRED_STLB_MISS_STORE    0x12d0

// freq_mon.c
extern int kfreqmond_init(pid_t pid, int node);
extern void kfreqmond_exit(void);
extern void pebs_update_period(int value);

#endif /* CONFIG_FREQ_PROMOTION */