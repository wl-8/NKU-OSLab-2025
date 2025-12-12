#include <defs.h>
#include <list.h>
#include <proc.h>
#include <assert.h>
#include <default_sched.h>
#include <stdio.h>

#define USE_SKEW_HEAP 1

/* 你应该在这里定义 BigStride 常量 */
/* LAB6 挑战1：2311208 */
#define BIG_STRIDE 0x7FFFFFFF /* 2^31-1，保证stride差值在有符号32位整数范围内 */

/* 两个 skew_heap_node_t 以及对应进程的比较函数 */
static int
proc_stride_comp_f(void *a, void *b)
{
     struct proc_struct *p = le2proc(a, lab6_run_pool);
     struct proc_struct *q = le2proc(b, lab6_run_pool);
     int32_t c = p->lab6_stride - q->lab6_stride;
     if (c > 0)
          return 1;
     else if (c == 0)
          return 0;
     else
          return -1;
}

/*
 * stride_init 初始化运行队列 rq 并正确赋值成员变量，包括：
 *
 *   - run_list: 初始化后应为空列表。
 *   - lab6_run_pool: NULL
 *   - proc_num: 0
 *   - max_time_slice: 在这里无需设置，该变量由调用者分配。
 *
 * 提示：参见 libs/list.h 获取列表结构的例程。
 */
static void
stride_init(struct run_queue *rq)
{
     /* LAB6 挑战1：2311208
      * (1) 初始化就绪进程列表：rq->run_list
      * (2) 初始化运行池：rq->lab6_run_pool (空堆用NULL表示)
      * (3) 将进程数量 rq->proc_num 设为 0
      */
     list_init(&(rq->run_list));
     rq->lab6_run_pool = NULL;
     rq->proc_num = 0;
}

/*
 * stride_enqueue 将进程 ``proc'' 插入运行队列 ``rq''。该过程应验证/初始化
 * proc 的相关成员，然后将 ``lab6_run_pool'' 节点放入队列（因为我们使用优先队列）。
 * 该过程还应更新 rq 结构中的元数据。
 *
 * proc->time_slice 表示为该进程分配的时间片，应设置为 rq->max_time_slice。
 *
 * 提示：参见 libs/skew_heap.h 中优先队列结构的例程。
 */
static void
stride_enqueue(struct run_queue *rq, struct proc_struct *proc)
{
     /* LAB6 挑战1：2311208
      * (1) 将 proc 正确插入 rq
      * 注意：你可以使用 skew_heap 或 list。重要函数：
      *         skew_heap_insert: 将一项插入 skew_heap
      *         list_add_before: 在列表末尾插入一项
      * (2) 重新计算 proc->time_slice
      * (3) 将 proc->rq 指向 rq
      * (4) 增加 rq->proc_num
      */
     rq->lab6_run_pool = skew_heap_insert(rq->lab6_run_pool, &(proc->lab6_run_pool), proc_stride_comp_f);
     proc->time_slice = rq->max_time_slice;
     proc->rq = rq;
     rq->proc_num += 1;
}

/*
 * stride_dequeue 从运行队列 ``rq'' 中移除进程 ``proc''，该操作可由 skew_heap_remove
 * 完成。记得更新 ``rq'' 结构。
 *
 * 提示：参见 libs/skew_heap.h 中优先队列结构的例程。
 */
static void
stride_dequeue(struct run_queue *rq, struct proc_struct *proc)
{
     /* LAB6 挑战1：2311208
      * (1) 正确地从 rq 中移除 proc
      * 注意：你可以使用 skew_heap 或 list。重要函数：
      *         skew_heap_remove: 从 skew_heap 中移除一项
      *         list_del_init: 从列表中移除一项并初始化
      */
     rq->lab6_run_pool = skew_heap_remove(rq->lab6_run_pool, &(proc->lab6_run_pool), proc_stride_comp_f);
     rq->proc_num -= 1;
}
/*
 * stride_pick_next 从运行队列中选取 stride 值最小的元素，并返回对应的进程指针。
 * 进程指针可通过宏 le2proc 计算，定义参见 kern/process/proc.h。若队列为空则返回 NULL。
 *
 * 当选中一个 proc 结构时，记得更新该 proc 的 stride 属性。（stride += BIG_STRIDE / priority）
 *
 * 提示：参见 libs/skew_heap.h 中优先队列结构的例程。
 */
static struct proc_struct *
stride_pick_next(struct run_queue *rq)
{
     /* LAB6 挑战1：2311208
      * (1) 获取 stride 值最小的 proc_struct 指针 p
             (1.1) 如果使用 skew_heap，可用 le2proc 从 rq->lab6_run_pool 获取 p
             (1.2) 如果使用列表，需要遍历列表找到具有最小 stride 值的 p
      * (2) 更新 p 的 stride 值：p->lab6_stride
      * (3) 返回 p
      */
     if (rq->lab6_run_pool == NULL) {
          return NULL;
     }
     struct proc_struct *p = le2proc(rq->lab6_run_pool, lab6_run_pool);
     p->lab6_stride += BIG_STRIDE / p->lab6_priority;
     return p;
}

/*
 * stride_proc_tick 处理当前进程的时钟 tick 事件。你应检查当前进程的时间片是否耗尽并更新 proc 结构。
 * proc->time_slice 表示当前进程剩余的时间片。proc->need_resched 是进程切换的标志变量。
 */
static void
stride_proc_tick(struct run_queue *rq, struct proc_struct *proc)
{
     /* LAB6 挑战1：2311208 */
     if (proc->time_slice > 0) {
          proc->time_slice -= 1;
     }
     if (proc->time_slice == 0) {
          proc->need_resched = 1;
     }
}

struct sched_class stride_sched_class = {
    .name = "stride_scheduler",
    .init = stride_init,
    .enqueue = stride_enqueue,
    .dequeue = stride_dequeue,
    .pick_next = stride_pick_next,
    .proc_tick = stride_proc_tick,
};
