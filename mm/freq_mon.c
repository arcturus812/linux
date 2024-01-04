/**
 * @file freq_mon.c
 * @author hwp (hwpark@dgist.ac.kr)
 * @brief functions of "spatial-locality aware memory management".
 * @version 0.1
 * @date 2023-12-06
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#ifdef CONFIG_FREQ_PROMOTION
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/ftrace.h>
#include <linux/perf_event.h>
#include <linux/memcontrol.h>
#include <linux/mempolicy.h>
#include <linux/mm.h>

#include "../kernel/events/internal.h"

#include <linux/samm.h>

struct task_struct *freq_mond = NULL;
struct perf_event **mem_event;

void pebs_update_period(int value){
    int cpu, ret;
    for(cpu = 0;cpu<NUM_CPU_ON_SOCKET;cpu++){
        ret = perf_event_period(mem_event[cpu], value);
        if(ret == -EINVAL){
            printk("failed to update PEBS sample period at %d\n", cpu);
        }else{
            printk("[PHW]PEBS period updated:%d\n", value);
        }
    }
}

static int __samm_perf_event_open(__u64 config, __u64 config1, __u64 cpu, __u32 pid){
    struct perf_event_attr attr;
    struct file *file;
    int fd, __pid;
    memset(&attr, 0, sizeof(struct perf_event_attr));

    attr.type = PERF_TYPE_RAW;
    attr.size = sizeof(struct perf_event_attr);
    attr.config = config;
    attr.config1 = config1;
    attr.sample_period = PEBS_SAMPLE_PERIOD;
    attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_ADDR | PERF_SAMPLE_PHYS_ADDR;
    attr.disabled = 0;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.exclude_callchain_kernel = 1;
    attr.exclude_callchain_user = 1;
    attr.precise_ip = 1;
    attr.enable_on_exec = 1;

    if(pid == 0){
        __pid = -1;
    }else{
        __pid = pid;
    }

    fd = samm_perf_event_open(&attr, __pid, cpu, -1, 0);
    if(fd <= 0){
        printk("perf_event_open fail fd:%d\n", fd);
        return -1;
    }

    file = fget(fd);
    if(!file){
        printk("perf_event_open invalid file\n");
        return -1;
    }
    mem_event[cpu] = fget(fd)->private_data; // should call again "fget"?

    return 0;
}

static int pebs_init(pid_t pid, int node){
    int cpu;
    mem_event = kzalloc(sizeof(struct perf_event *) * NUM_CPU_ON_SOCKET, GFP_KERNEL);
    for(cpu = 0; cpu < NUM_CPU_ON_SOCKET; cpu++){
        if(__samm_perf_event_open(MEM_LOAD_L3_MISS_REMOTE, 0, cpu, pid)){
            return -1;
        }
        if(samm_perf_event_init(mem_event[cpu], PEBS_BUF_SIZE)){
            return -1;
        }
    }

    return 0;
}

static void pebs_disable(void){
    int cpu;
    printk("disable PEBS\n");
    for(cpu = 0;cpu<NUM_CPU_ON_SOCKET;cpu++){
        if(mem_event[cpu]){
            perf_event_disable(mem_event[cpu]);
        }
    }
}
static void pebs_enable(void){
    int cpu;
    printk("enable PEBS\n");
    for(cpu = 0;cpu<NUM_CPU_ON_SOCKET;cpu++){
        if(mem_event[cpu]){
            perf_event_enable(mem_event[cpu]);
        }
    }
}

static int kfreqmond(void *data){
    while(!kthread_should_stop()){
        int cpu;
        int remain = false;
        // msleep_interruptible(PEBS_SLEEP_DURATION);
        schedule_timeout_interruptible(PEBS_SLEEP_DURATION);
        for(cpu=0;cpu<NUM_CPU_ON_SOCKET;cpu++){
            do{
                struct perf_buffer *rb;
                struct perf_event_mmap_page *up;
                struct perf_event_header *ph;
                struct samm_event *se;
                __u64 head;
                int page_shift;
                unsigned long pg_idx, offset;
                struct page *page;
                unsigned long pfn;
                __sync_synchronize();

                if(!mem_event[cpu]){
                    continue;
                }
                rb = mem_event[cpu]->rb;
                if(!rb){
                    printk("rb is NULL\n");
                    return -1;
                }
                up = READ_ONCE(rb->user_page);
                head = READ_ONCE(up->data_head);
                if(head == up->data_tail){
                    remain = false;
                }else{
                    remain = true;
                }
                smp_rmb();
                page_shift = PAGE_SHIFT + page_order(rb);
                offset = READ_ONCE(up->data_tail); 
                pg_idx = (offset >> page_shift) & (rb->nr_pages -1);
                offset &= (1 << page_shift) - 1;

                ph = (void *)(rb->data_pages[pg_idx] + offset);
                switch(ph->type){
                    case PERF_RECORD_SAMPLE:
                        se = (struct samm_event *)ph;
                        // need address validation?
                        // TODO update page info 
                        pfn = se->phys_addr >> PAGE_SHIFT;
                        if(!pfn_valid(pfn)){
                            printk("invalid pfn detected:0x%lx at core:%d\n", pfn, cpu);
                            return -1;
                        }
                        page = pfn_to_page(pfn);
                        if(page->freq < 255){
                            page->freq++;
                        }
                        // trace_printk("[PHW]core:%d\tpid:%u\ttid:%u\tva:0x%llx\tpa:0x%llx\n", cpu, se->pid, se->tid, se->addr, se->phys_addr);
                        trace_printk("[PHW]core:%d\tpid:%u\tva:0x%llx\tpfn:0x%lx\tfreq:%u\n", cpu, se->pid, se->addr, pfn, page->freq);
                        break;
                }
                WRITE_ONCE(up->data_tail, up->data_tail + ph->size);
            }while(remain);
        }
    }
    return 0;
}

static int kfreqmond_run(void){
    int err = 0;
    if(!freq_mond){
	    freq_mond = kthread_run(kfreqmond, NULL, "kfreqmond");
	    if(IS_ERR(freq_mond)){
		    err = PTR_ERR(freq_mond);
		    freq_mond = NULL;
	    }
        trace_printk("[PHW]kfreqmond start\n");
    }
    return err;
}

int kfreqmond_init(pid_t pid, int node){
    int ret;
    ret = pebs_init(pid, node);
    if(ret){
        printk("pebs_init file, ERR:%d\n", ret);
        return 0;
    }
    return kfreqmond_run();
}

void kfreqmond_exit(void){
    if(freq_mond){
        kthread_stop(freq_mond);
        freq_mond = NULL;
        trace_printk("[PHW]kfreqmond end\n");
    }
}

#endif /* CONFIG_FREQ_PROMOTION */
