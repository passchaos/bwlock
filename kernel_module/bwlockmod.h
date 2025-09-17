#ifndef BWLOCKMOD_H
#define BWLOCKMOD_H

#include <asm/ptrace.h>
static int bwlock_init(void);
int lazy_init_thread(void *);
static void bwlock_cleanup(void);


void perfmod_process_overflow(struct irq_work*);
void event_overflow_callback(struct perf_event *,
    struct perf_sample_data *,
    struct pt_regs *);

#endif
