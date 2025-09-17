#ifndef COMMON_H
#define COMMON_H

#include <linux/hrtimer_types.h>
#include <linux/sched.h>
#include <linux/irq_work.h>
#include <linux/wait.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/kthread.h>
#include <linux/perf_event.h>
#include <linux/sched/prio.h>

#include <linux/types.h>
#include <linux/sched/rt.h>

#include <linux/percpu-defs.h>
#include <uapi/linux/perf_event.h>


struct perfmod_info {
    u64 system_throttle_duration;
    u64 system_throttle_period_cnt;
};

struct task_info {
    struct task_struct *throttle_task;
    int orig_nice_value;
};

struct core_info {
    // init thread
    struct task_struct *init_thread;

    // throttle related
    struct task_struct *throttle_thread;
    wait_queue_head_t throttle_event;
    struct task_struct *throttle_task;
    // struct task_info *throttle_task;
    int throttle_core;

    struct irq_work pending_work;
    struct perf_event *event;
    u64 budget;

    // hrtimer related
    ktime_t period_in_ktime;
    struct hrtimer hrtimer;
};

extern int nr_bwlocked_cores(void);

int init_bwlock_controlfs(void);

int throttle_thread_fn(void *);

enum hrtimer_restart periodic_timer_callback (struct hrtimer *);

u64 convert_mb_to_events(u64);
struct perf_event* init_counter(int, int);
void start_counters(void);
void __start_counter(void *);
void disable_counters(void);

#endif
