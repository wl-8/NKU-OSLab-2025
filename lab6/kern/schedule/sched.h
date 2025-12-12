#ifndef __KERN_SCHEDULE_SCHED_H__
#define __KERN_SCHEDULE_SCHED_H__

#include <defs.h>
#include <list.h>
#include <skew_heap.h>

#define MAX_TIME_SLICE 5

struct proc_struct;

struct run_queue;

// 调度类的引入借鉴了 Linux，使核心调度器具有很好的可扩展性。
// 这些类（调度模块）封装了调度策略。
struct sched_class
{
    // 调度类的名称
    const char *name;
    // 初始化运行队列
    void (*init)(struct run_queue *rq);
    // 将进程放入运行队列，必须在持有 rq_lock 时调用
    void (*enqueue)(struct run_queue *rq, struct proc_struct *proc);
    // 将进程从运行队列移出，必须在持有 rq_lock 时调用
    void (*dequeue)(struct run_queue *rq, struct proc_struct *proc);
    // 选择下一个可运行的任务
    struct proc_struct *(*pick_next)(struct run_queue *rq);
    // 时间片处理函数
    void (*proc_tick)(struct run_queue *rq, struct proc_struct *proc);
    /* 为将来的 SMP 支持
     *  负载均衡
     *     void (*load_balance)(struct rq* rq);
     *  从此运行队列获取一些进程，用于负载均衡，
     *  返回值为获取到的进程数量
     *  int (*get_proc)(struct rq* rq, struct proc* procs_moved[]);
     */
};

struct run_queue
{
    list_entry_t run_list;
    unsigned int proc_num;
    int max_time_slice;
    // 仅用于 LAB6
    skew_heap_entry_t *lab6_run_pool;
};

void sched_init(void);
void wakeup_proc(struct proc_struct *proc);
void schedule(void);
void sched_class_proc_tick(struct proc_struct *proc);
#endif /* !__KERN_SCHEDULE_SCHED_H__ */
