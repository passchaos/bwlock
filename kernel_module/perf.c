#include "common.h"

extern struct core_info __percpu *core_info;


void print_task_id_info(void) {
    char comm[16];
    // 获取进程名称（也可直接用 current->comm）
    get_task_comm(comm, current);

    printk(KERN_INFO "=== 任务标识信息 ===");
    printk(KERN_INFO "PID:    %d", current->pid);       // 进程ID
    printk(KERN_INFO "TGID:   %d", current->tgid);      // 线程组ID（用户态线程共享）
    printk(KERN_INFO "名称:   %s", comm);               // 进程/线程名称
}

void perfmod_process_overflow(struct irq_work* entry)
{
    struct core_info *cinfo = this_cpu_ptr(core_info);

    // printk(KERN_INFO "irq on Core %d\n", smp_processor_id());

    cinfo->event->pmu->stop(cinfo->event, PERF_EF_UPDATE);
    local64_set(&cinfo->event->hw.period_left, 0xffffffff);
    cinfo->event->pmu->start(cinfo->event, PERF_EF_RELOAD);

    if (!rt_task(current)) {
        cinfo->throttle_core = 1;
        smp_mb();

        // int orig_nice_value = task_nice(current);

        // struct task_info task_i = {
        //     .orig_nice_value = orig_nice_value,
        //     .throttle_task = current,
        // };
        cinfo->throttle_task = current;

        // printk(KERN_INFO "need wake up %d", smp_processor_id());
        wake_up_interruptible(&cinfo->throttle_event);
    }

    return;
}

void event_overflow_callback(struct perf_event *event,
    struct perf_sample_data *data,
    struct pt_regs *regs)
{
    struct core_info *cinfo = this_cpu_ptr(core_info);
    irq_work_queue(&cinfo->pending_work);

    return;
}

struct perf_event* init_counter(int cpu, int budget)
{
    // jetson需要使用PERF_TYPE_RAW配合0x2a
    struct perf_event_attr hw_attr = {
        // .type = PERF_TYPE_SOFTWARE,
        // .type = PERF_TYPE_RAW,
        .type = PERF_TYPE_HW_CACHE,
        // .config = PERF_COUNT_HW_CACHE_REFERENCES | PERF_COUNT_HW_CACHE_MISSES,
        // .config = PERF_COUNT_SW_CPU_CLOCK,
        .config = PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
        // .config = 0x2a, // L3d cache refill
        // .config = 0x17, // L2d cache refill
        .size = sizeof(struct perf_event_attr),
        .pinned = 1,
        .disabled = 1,
        .exclude_kernel = 1,
        .sample_period = budget
    };

    struct perf_event* event = perf_event_create_kernel_counter(&hw_attr, cpu, NULL, event_overflow_callback, NULL);
    printk(KERN_INFO "create perf event");
    return event;
}

int throttle_thread_fn(void * arg)
{
    int cpunr = (unsigned long)arg;
    int i = smp_processor_id();

    printk(KERN_INFO "throttle thread begin: %d %d", cpunr, i);

    struct core_info *cinfo = this_cpu_ptr(core_info);

    while (!kthread_should_stop() && cpu_online(cpunr)) {
        // printk(KERN_INFO "wait for throttle event: cpu %d %d", cpunr, i);

        int ret = wait_event_interruptible(cinfo->throttle_event, cinfo->throttle_core || kthread_should_stop());

        // printk(KERN_INFO "begin throttle event: cpu %d %d ret: %d", cpunr, i, ret);

        if (kthread_should_stop()) {
            break;
        }

        smp_mb();

        ktime_t begin = ktime_get();
        while (cinfo->throttle_core && !kthread_should_stop()) {
            cpu_relax();
            smp_mb();
        }

        ktime_t end = ktime_get();
        u64 delta_ns = ktime_to_ns(ktime_sub(end, begin));

        // printk(KERN_INFO "finish cpu relax: cpu %d %d delta: %llu", cpunr, i, delta_ns);


        if (cinfo->throttle_task) {
            // printk(KERN_INFO "enlarge throttle task se vruntime: %d\n", delta_ns);
            // 直接更改vruntime可能会导致无定义行为（内部调度红黑树冲突）
            // cinfo->throttle_task->se.vruntime += 3 * delta_ns;
            //
            set_user_nice(cinfo->throttle_task, 19);
            smp_mb();

            cinfo->throttle_task = NULL;
        } else {
            printk(KERN_ERR "No throttle task found\n");
        }

    }

    printk(KERN_INFO "throttle thread end: %d %d", cpunr, i);
    return 0;
}


enum hrtimer_restart periodic_timer_callback(struct hrtimer *timer)
{
    struct core_info *cinfo = this_cpu_ptr(core_info);
    int i = smp_processor_id();

    int over_run_cnt = hrtimer_forward_now(timer, cinfo->period_in_ktime);
    if (over_run_cnt == 0) {
        goto done;
    }

    // 每次触发时暂停perf_event相关计数，这样避免perf_event计数触发是累计触发，
    // 比如内存速率不高，但是时间长了还是会触发阈值
    // 在事件触发的中断处理中，将period_left设置为一个很大的数
    // 如果这里不进行重新设置，那么不会再次收到触发事件的
    cinfo->event->pmu->stop(cinfo->event, PERF_EF_UPDATE);
    cinfo->event->hw.sample_period = cinfo->budget;
    local64_set(&cinfo->event->hw.period_left, cinfo->budget);
    cinfo->event->pmu->start(cinfo->event, PERF_EF_RELOAD);

    if (cinfo->throttle_core) {
        // printk(KERN_INFO "reset throttle task flag %d\n", i);

        cinfo->throttle_core = 0;
    }

    if (cinfo->throttle_task) {
        set_user_nice(cinfo->throttle_task, 0);
    }

    smp_mb();

    //
    done:
    return HRTIMER_RESTART;
}




void __start_counter(void *info)
{
    struct core_info *cinfo = this_cpu_ptr(core_info);
    perf_event_enable(cinfo->event);
    // cinfo->event->pmu->add(cinfo->event, PERF_EF_START);
    return;
}

void start_counters(void)
{
    on_each_cpu(__start_counter, NULL, 0);
    return;
}

void __disable_counter(void *info)
{
    struct core_info *cinfo = this_cpu_ptr(core_info);
    cinfo->event->pmu->stop(cinfo->event, PERF_EF_UPDATE);
    cinfo->event->pmu->del(cinfo->event, 0);

    return;
}

void disable_counters(void)
{
    on_each_cpu(__disable_counter, NULL, 0);
    return;
}
