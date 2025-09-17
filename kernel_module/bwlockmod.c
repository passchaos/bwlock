#include "common.h"
#include "bwlockmod.h"

struct perfmod_info perfmod_info;
struct core_info __percpu *core_info;

int				g_period_us 			= 1000;


static int bwlock_init(void)
{
    printk(KERN_INFO "init bandwidth lock module\n");

    int i, ret = 0;
    struct perfmod_info *global = &perfmod_info;
    memset(global, 0, sizeof(struct perfmod_info));

    core_info = alloc_percpu(struct core_info);
    smp_mb();

    // i = 3;
    // rcu_read_lock();
    for_each_online_cpu(i) {
        struct core_info *cinfo = per_cpu_ptr(core_info, i);
        memset(cinfo, 0, sizeof(struct core_info));
        smp_mb();

        cinfo->init_thread = kthread_create_on_node(lazy_init_thread,
            (void *)((unsigned long) i),
            cpu_to_node(i),
            "kinit/%d", i
            );
        kthread_bind(cinfo->init_thread, i);
        wake_up_process(cinfo->init_thread);
    }
    // rcu_read_unlock();

    return 0;
}

int lazy_init_thread(void *arg)
{
    struct core_info *cinfo = this_cpu_ptr(core_info);
    int i = smp_processor_id();

    memset(cinfo, 0, sizeof(struct core_info));
    smp_mb();

    int node = cpu_to_node(i);
    printk(KERN_INFO "init core %d node %d\n", i, node);

    printk(KERN_INFO "lazy init thread %d!\n", i);

    smp_rmb();

    cinfo->throttle_core = 0;
    cinfo->throttle_task = NULL;
    // cinfo->throttle
    //


    // 配置限流监听逻辑
    init_waitqueue_head(&cinfo->throttle_event);
    // 配置软中断处理
    init_irq_work(&cinfo->pending_work, perfmod_process_overflow);

    // 配置监听lcc
    // u64 budget = convert_mb_to_events(1);
    u64 budget = 1638;
    printk(KERN_INFO "event limit: %llu", budget);
    cinfo->budget = budget;

    smp_wmb();
    cinfo->event = init_counter(i, budget);


    if (IS_ERR(cinfo->event)) {
        printk(KERN_ERR "Failed to initialize counter for core %d: %ld\n", i, PTR_ERR(cinfo->event));
        return -1;
    } else {
        printk(KERN_INFO "Initialized counter for core %d\n", i);
    }

    perf_event_enable(cinfo->event);
    // cinfo->event->pmu->add(cinfo->event, PERF_EF_START);
    //

    // 开启限流处理逻辑
    cinfo->throttle_thread = kthread_create_on_node(throttle_thread_fn, (void *)(unsigned long)i, cpu_to_node(i), "kthrottle/%d", i);

    struct sched_param param = {
        .sched_priority = MAX_RT_PRIO / 2
    };
    cinfo->throttle_thread->policy = SCHED_FIFO;
    cinfo->throttle_thread->rt_priority = param.sched_priority;

    kthread_bind(cinfo->throttle_thread, i);
    wake_up_process(cinfo->throttle_thread);


    // 开启hrtimer
    // 使用init_thread独立线程进行初始化，为的就是hrtimer能在对应cpu的上下文中运行
    cinfo->period_in_ktime = ktime_set(0, 1000 * 1000);
    hrtimer_init(&cinfo->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_PINNED);
    (&cinfo->hrtimer)->function = &periodic_timer_callback;
    hrtimer_start(&cinfo->hrtimer, cinfo->period_in_ktime, HRTIMER_MODE_REL_PINNED);

    // __start_counter(NULL);
    printk(KERN_INFO "Started counter for core %d\n", i);

    // init_waitqueue_head(&cinfo->throttle_event);

    return 0;
}


static void bwlock_cleanup(void)
{
    printk(KERN_INFO "cleanup bandwidth lock module\n");

    int i;
    // struct perfmod_info *global = &perfmod_info;

    smp_mb();

    // // rcu_read_lock();
    disable_counters();

    for_each_online_cpu(i) {
        struct core_info *cinfo = per_cpu_ptr(core_info, i);

        hrtimer_cancel(&cinfo->hrtimer);

        perf_event_disable(cinfo->event);
        perf_event_release_kernel(cinfo->event);

        cinfo->throttle_core = 0;
        smp_mb();

        kthread_stop(cinfo->throttle_thread);

        printk(KERN_INFO "Released counter for core %d\n", i);
    }

    free_percpu(core_info);
    // rcu_read_unlock();

    printk(KERN_INFO "Cleanup completed\n");
    return;
}

module_init(bwlock_init);
module_exit(bwlock_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("masai.dss");
MODULE_DESCRIPTION("A simple bandwidth lock module");
