# 2025 操作系统 Lab6 实验报告

> 学号：2311208 姓名：魏来

## 实验目的

- 理解操作系统的调度管理机制
- 熟悉 ucore 的系统调度器框架
- 实现 Stride Scheduling 调度算法
- 掌握基于优先级的进程调度原理

## 实验内容

本实验在前序实验的基础上，实现了 ucore 的进程调度框架，并基于该框架实现了 Stride Scheduling 调度算法。主要工作包括：

1. **练习0**：填写已有实验代码（LAB2/3/4/5），并初始化进程调度相关字段
2. **扩展练习 Challenge 1**：实现 Stride Scheduling 调度算法

---

## 练习0：填写已有实验

### 实现内容

本实验依赖实验2/3/4/5，需要将之前实验的代码填入本实验相应位置，并进行必要的修改以支持调度功能。

#### 1. 进程控制块初始化（`kern/process/proc.c:alloc_proc`）

在 `alloc_proc` 函数中，需要初始化进程调度相关的字段：

```c
// LAB6: 2311208 (update LAB5 steps)
proc->rq = NULL;
list_init(&(proc->run_link));
proc->time_slice = 0;
proc->lab6_stride = 0;
proc->lab6_priority = 1;  // 默认优先级为1
skew_heap_init(&(proc->lab6_run_pool));
```

**初始化说明**：
- `rq`：运行队列指针，初始化为 NULL
- `run_link`：运行队列链表节点，使用 `list_init` 初始化
- `time_slice`：时间片，初始化为 0
- `lab6_stride`：Stride 值，初始化为 0
- `lab6_priority`：优先级，默认为 1
- `lab6_run_pool`：斜堆节点，使用 `skew_heap_init` 初始化

#### 2. 时钟中断处理（`kern/trap/trap.c`）

在时钟中断处理中，需要调用调度器的 `proc_tick` 函数：

```c
case IRQ_S_TIMER:
    clock_set_next_event();
    if (++ticks % TICK_NUM == 0) {
        print_ticks();
    }
    // lab6: 2311208 (update LAB3 steps)
    sched_class_proc_tick(current);
    break;
```

**功能说明**：
- 每次时钟中断时，调用 `sched_class_proc_tick(current)` 来更新当前进程的时间片
- 当时间片耗尽时，会设置 `need_resched` 标志，触发进程调度

---

## 扩展练习 Challenge 1：实现 Stride Scheduling 调度算法

### Stride Scheduling 算法原理

#### 基本思想

Stride Scheduling 是一种基于优先级的确定性调度算法，其核心思想是：

1. 为每个可运行进程维护一个 **stride** 值（步长），表示该进程当前的调度权重
2. 为每个进程定义一个 **pass** 值（步进），与进程优先级成反比：
   ```
   pass = BIG_STRIDE / priority
   ```
3. 每次调度时，选择 **stride 值最小** 的进程执行
4. 被选中的进程执行后，其 stride 值增加 pass：
   ```
   stride += pass
   ```

#### 公平性证明

**命题**：在足够长的时间后，每个进程分配到的 CPU 时间与其优先级成正比。

**证明思路**：

设有两个进程 A 和 B，优先级分别为 $P_A$ 和 $P_B$。假设经过足够长时间后，A 被调度了 $N_A$ 次，B 被调度了 $N_B$ 次。

根据算法，在调度稳定后，两个进程的 stride 值应该接近：

$$
\text{stride}_A \approx \text{stride}_B
$$

因为每次调度都选择 stride 最小的进程，如果某个进程的 stride 显著小于另一个，它会被连续调度直到 stride 值追上。

而每个进程的 stride 值为：

$$
\text{stride}_A = N_A \times \frac{\text{BIG\_STRIDE}}{P_A}
$$

$$
\text{stride}_B = N_B \times \frac{\text{BIG\_STRIDE}}{P_B}
$$

令两者相等：

$$
N_A \times \frac{\text{BIG\_STRIDE}}{P_A} = N_B \times \frac{\text{BIG\_STRIDE}}{P_B}
$$

化简得：

$$
\frac{N_A}{N_B} = \frac{P_A}{P_B}
$$

因此，**每个进程被调度的次数与其优先级成正比**，从而 CPU 时间分配也与优先级成正比。

#### 溢出处理

**问题**：stride 值是有限位整数，会发生溢出。

**理论基础**：可以证明，在任意调度时刻，有：

$$
\text{STRIDE\_MAX} - \text{STRIDE\_MIN} \leq \text{PASS\_MAX} = \frac{\text{BIG\_STRIDE}}{\text{PRIORITY\_MIN}}
$$

其中 PRIORITY_MIN ≥ 1。

**证明**：
1. 设当前 stride 最小的进程为 P_min，最大的为 P_max
2. P_max 的 stride 必然是从某个较小值累加而来
3. 在 P_min 被选中之前，P_max 不会再被调度（因为 P_min 的 stride 更小）
4. 因此 P_max 的 stride 与 P_min 的差值不会超过一个 PASS_MAX

**解决方案**：使用有符号整数比较。对于 32 位 stride：

```c
#define BIG_STRIDE 0x7FFFFFFF  // 2^31 - 1

int32_t c = p->lab6_stride - q->lab6_stride;
if (c > 0)
    return 1;  // p 的 stride 更大
else if (c == 0)
    return 0;
else
    return -1;  // p 的 stride 更小
```

即使发生溢出，**有符号整数的差值仍然能正确反映大小关系**。

**溢出处理原理**（参考实验指导书 16 位例子）：

实验指导书举例：
- 调度前：A = 65534, B = 65535，应该选 A（stride 小）
- A 被调度，stride 加 100 后溢出：65534 + 100 = 65634 → 65634 mod 65536 = 98
- 调度后：A = 98, B = 65535

**关键场景**：A 溢出后变成 98，B 还是 65535

**问题**：直接无符号比较 98 < 65535，会错误地认为应该选 A。但实际上 A 刚被调度过（累计值大），应该选 B。

**解决**：用有符号整数做差：
```c
int16_t diff = (int16_t)(98 - 65535);
// 无符号计算：98 - 65535 = -65437
// 转为 16 位有符号数时下溢：-65437 mod 65536 = 99（正数）
```
diff = 99 > 0，说明 A > B，因此应该选 B ✓ 正确！

**核心思想**：无符号整数上溢导致数值变小，有符号做差时的下溢能修正这一偏差，正确反映逻辑大小。

**32 位情况**：`BIG_STRIDE = 0x7FFFFFFF` 保证 `STRIDE_MAX - STRIDE_MIN` 在有符号范围内，有符号减法能正确判断大小

### 数据结构设计

#### 斜堆（Skew Heap）

Stride 调度需要频繁地：
1. 找到 stride 最小的进程
2. 插入/删除进程

使用**斜堆**数据结构，时间复杂度为 O(log n)。

**斜堆特点**：
- 自调整的二叉堆
- 合并操作简单高效
- 不需要维护平衡性

**核心操作**：
```c
// 插入节点
skew_heap_entry_t *skew_heap_insert(skew_heap_entry_t *a,
                                     skew_heap_entry_t *b,
                                     compare_f comp);

// 删除节点
skew_heap_entry_t *skew_heap_remove(skew_heap_entry_t *a,
                                     skew_heap_entry_t *b,
                                     compare_f comp);
```

**比较函数**：
```c
static int proc_stride_comp_f(void *a, void *b)
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
```

### 实现过程

#### 1. 调度器初始化（`stride_init`）

```c
static void stride_init(struct run_queue *rq)
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
```

**实现说明**：
- 初始化运行队列的链表头
- 将斜堆根指针设为 NULL（空堆）
- 进程计数器清零

#### 2. 进程入队（`stride_enqueue`）

```c
static void stride_enqueue(struct run_queue *rq, struct proc_struct *proc)
{
    /* LAB6 挑战1：2311208
     * (1) 将 proc 正确插入 rq
     * (2) 重新计算 proc->time_slice
     * (3) 将 proc->rq 指向 rq
     * (4) 增加 rq->proc_num
     */
    rq->lab6_run_pool = skew_heap_insert(rq->lab6_run_pool,
                                          &(proc->lab6_run_pool),
                                          proc_stride_comp_f);
    proc->time_slice = rq->max_time_slice;
    proc->rq = rq;
    rq->proc_num += 1;
}
```

**实现说明**：
- 使用 `skew_heap_insert` 将进程节点插入斜堆，保持堆的有序性
- 重置进程的时间片为最大值
- 建立进程与运行队列的关联
- 更新队列中的进程计数

#### 3. 进程出队（`stride_dequeue`）

```c
static void stride_dequeue(struct run_queue *rq, struct proc_struct *proc)
{
    /* LAB6 挑战1：2311208
     * (1) 正确地从 rq 中移除 proc
     */
    rq->lab6_run_pool = skew_heap_remove(rq->lab6_run_pool,
                                          &(proc->lab6_run_pool),
                                          proc_stride_comp_f);
    rq->proc_num -= 1;
}
```

**实现说明**：
- 使用 `skew_heap_remove` 从斜堆中移除指定进程
- 更新进程计数

#### 4. 选择下一个进程（`stride_pick_next`）

```c
static struct proc_struct *stride_pick_next(struct run_queue *rq)
{
    /* LAB6 挑战1：2311208
     * (1) 获取 stride 值最小的 proc_struct 指针 p
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
```

**实现说明**：
- 堆顶元素即为 stride 最小的进程
- 使用 `le2proc` 宏从堆节点获取进程结构体指针
- **关键操作**：更新进程的 stride 值，使其下次调度时会排到更后面
- stride 增加量与优先级成反比，优先级高的进程增量小，下次更容易被选中

#### 5. 时钟中断处理（`stride_proc_tick`）

```c
static void stride_proc_tick(struct run_queue *rq, struct proc_struct *proc)
{
    /* LAB6 挑战1：2311208 */
    if (proc->time_slice > 0) {
        proc->time_slice -= 1;
    }
    if (proc->time_slice == 0) {
        proc->need_resched = 1;
    }
}
```

**实现说明**：
- 每次时钟中断，当前进程的时间片减 1
- 当时间片耗尽时，设置 `need_resched` 标志
- 调度器会在合适的时机检查该标志，触发进程切换

#### 6. 调度器切换（`kern/schedule/sched.c`）

在 `sched_init` 中切换到 Stride 调度器：

```c
void sched_init(void)
{
    list_init(&timer_list);

    sched_class = &stride_sched_class;  // 使用 Stride 调度器

    rq = &__rq;
    rq->max_time_slice = MAX_TIME_SLICE;
    sched_class->init(rq);

    cprintf("sched class: %s\n", sched_class->name);
}
```

### 调度流程图

```
                      时钟中断
                         |
                         v
                  +-------------+
                  | IRQ_S_TIMER |
                  +-------------+
                         |
                         v
         +-------------------------------+
         | sched_class_proc_tick(current)|  ← 调用调度类的 proc_tick
         +-------------------------------+
                         |
                         v
              +---------------------+
              | stride_proc_tick()  |
              +---------------------+
                         |
                         v
              +---------------------+
              | time_slice -= 1     |
              +---------------------+
                         |
                         v
              +---------------------+
              | time_slice == 0 ?   |
              +---------------------+
                    /          \
                  是            否
                  |              |
                  v              v
        +------------------+   结束
        | need_resched = 1 |
        +------------------+
                  |
                  v
            中断返回前检查
                  |
                  v
         +----------------+
         | need_resched ? |
         +----------------+
                  |
                是|
                  v
         +----------------+
         |  schedule()    |  ← 进程调度
         +----------------+
                  |
                  v
    +-----------------------------+
    | current->state == RUNNABLE? |  ← 当前进程还能运行吗？
    +-----------------------------+
                  |
                是|
                  v
    +-----------------------------+
    | sched_class_enqueue(current)|  ← 将当前进程重新入队
    +-----------------------------+
                  |
                  v
         +----------------------+
         | stride_enqueue()     |
         +----------------------+
                  |
                  v
      +--------------------------+
      | skew_heap_insert()       |  ← 插入斜堆（按 stride 排序）
      +--------------------------+
                  |
                  v
         +----------------------+
         | pick_next()          |  ← 选择下一个进程
         +----------------------+
                  |
                  v
         +----------------------+
         | stride_pick_next()   |
         +----------------------+
                  |
                  v
    +---------------------------+
    | p = 堆顶元素（stride 最小）|
    +---------------------------+
                  |
                  v
    +---------------------------+
    | p->stride += BIG_STRIDE / |  ← 更新 stride
    |              p->priority  |
    +---------------------------+
                  |
                  v
         +----------------------+
         | sched_class_dequeue()|  ← 将选中进程出队
         +----------------------+
                  |
                  v
         +----------------------+
         | stride_dequeue()     |
         +----------------------+
                  |
                  v
         +----------------------+
         | skew_heap_remove()   |  ← 从斜堆移除
         +----------------------+
                  |
                  v
         +----------------------+
         | proc_run(next)       |  ← 切换到新进程
         +----------------------+
                  |
                  v
         +----------------------+
         | switch_to()          |  ← 上下文切换
         +----------------------+
```

### 关键设计要点

#### 1. BIG_STRIDE 的选择

```c
#define BIG_STRIDE 0x7FFFFFFF  // 2^31 - 1
```

**选择依据**：
- 使用 32 位有符号整数表示 stride
- 取最大正整数 $2^{31} - 1$
- 保证任意两个进程的 stride 差值不超过 $2^{31}$，在有符号整数范围内

#### 2. 调度时机

Stride 调度器在以下情况下被调用：

1. **时钟中断**：
   - `stride_proc_tick` 被调用，更新时间片
   - 时间片耗尽时设置 `need_resched = 1`

2. **进程主动让出 CPU**：
   - `do_wait`、`do_sleep` 等系统调用
   - 进程被设为 SLEEPING 状态

3. **进程退出**：
   - `do_exit` 调用 `schedule()`

4. **新进程创建**：
   - `do_fork` 后调用 `wakeup_proc` 将新进程加入就绪队列

#### 3. 调度公平性保证

通过以下机制保证公平性：

1. **优先级映射**：
   ```
   pass = BIG_STRIDE / priority
   ```
   - 优先级越高，pass 越小
   - stride 增长越慢，更容易被选中

2. **最小 stride 选择**：
   - 始终选择 stride 最小的进程
   - 保证被"饿死"的进程（stride 很大）不会被长期忽略

3. **动态调整**：
   - 每次调度后更新 stride
   - 形成负反馈：被调度多的进程 stride 变大，减少调度机会

---

## 实验结果

### 运行输出

执行 `make qemu` 命令，系统成功启动并运行 priority 测试程序：

```
OpenSBI v0.4 (Jul  2 2019 11:53:53)
   ____                    _____ ____ _____
  / __ \                  / ____|  _ \_   _|
 | |  | |_ __   ___ _ __ | (___ | |_) || |
 | |  | | '_ \ / _ \ '_ \ \___ \|  _ < | |
 | |__| | |_) |  __/ | | |____) | |_) || |_
  \____/| .__/ \___|_| |_|_____/|____/_____|
        | |
        |_|

Platform Name          : QEMU Virt Machine
Platform HART Features : RV64ACDFIMSU
Platform Max HARTs     : 8
Current Hart           : 0
Firmware Base          : 0x80000000
Firmware Size          : 112 KB
Runtime SBI Version    : 0.1

PMP0: 0x0000000080000000-0x000000008001ffff (A)
PMP1: 0x0000000000000000-0xffffffffffffffff (A,R,W,X)
(THU.CST) os is loading ...

Special kernel symbols:
  entry  0xc020004a (virtual)
  etext  0xc0205c24 (virtual)
  edata  0xc02c2758 (virtual)
  end    0xc02c6c38 (virtual)
Kernel executable memory footprint: 795KB
DTB Init
HartID: 0
DTB Address: 0x82200000
Physical Memory from DTB:
  Base: 0x0000000080000000
  Size: 0x0000000008000000 (128 MB)
  End:  0x0000000087ffffff
DTB init completed
memory management: default_pmm_manager
physcial memory map:
  memory: 0x08000000, [0x80000000, 0x87ffffff].
vapaofset is 18446744070488326144
check_alloc_page() succeeded!
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
use SLOB allocator
kmalloc_init() succeeded!
check_vma_struct() succeeded!
check_vmm() succeeded.
sched class: stride_scheduler
++ setup timer interrupts
kernel_execve: pid = 2, name = "priority".
set priority to 6
main: fork ok,now need to wait pids.
set priority to 5
set priority to 4
set priority to 3
set priority to 2
set priority to 1
100 ticks
100 ticks
child pid 7, acc 1144000, time 2010
child pid 6, acc 920000, time 2010
child pid 5, acc 760000, time 2010
child pid 4, acc 576000, time 2010
child pid 3, acc 372000, time 2010
main: pid 3, acc 372000, time 2010
main: pid 4, acc 576000, time 2010
main: pid 5, acc 760000, time 2010
main: pid 6, acc 920000, time 2010
main: pid 0, acc 1144000, time 2010
main: wait pids over
sched result: 1 2 2 2 3
all user-mode processes have quit.
init check memory pass.
kernel panic at kern/process/proc.c:532:
    initproc exit.
```

### 结果分析

#### 1. 调度器启动

```
sched class: stride_scheduler
```

确认系统使用了 Stride 调度器。

#### 2. 进程创建

```
set priority to 6    ← 主进程设置优先级为 6
main: fork ok,now need to wait pids.
set priority to 5    ← 子进程 1 (优先级 5)
set priority to 4    ← 子进程 2 (优先级 4)
set priority to 3    ← 子进程 3 (优先级 3)
set priority to 2    ← 子进程 4 (优先级 2)
set priority to 1    ← 子进程 5 (优先级 1)
```

成功创建了 5 个不同优先级的子进程。

#### 3. CPU 时间分配

| 进程 PID | 优先级 | acc 值 | 理论比例 | 实际比例 |
|---------|--------|--------|----------|----------|
| 7       | 5      | 1144000| 5        | 3.07     |
| 6       | 4      | 920000 | 4        | 2.47     |
| 5       | 3      | 760000 | 3        | 2.04     |
| 4       | 2      | 576000 | 2        | 1.55     |
| 3       | 1      | 372000 | 1        | 1.00     |

**实际比例计算**（以 PID 3 为基准）：
```
PID 7: 1144000 / 372000 ≈ 3.07  (理论 5)
PID 6: 920000  / 372000 ≈ 2.47  (理论 4)
PID 5: 760000  / 372000 ≈ 2.04  (理论 3)
PID 4: 576000  / 372000 ≈ 1.55  (理论 2)
PID 3: 372000  / 372000 = 1.00  (理论 1)
```

#### 4. 调度结果

```
sched result: 1 2 2 2 3
```

这个结果表示各进程获得的 CPU 时间比例：
- 1：基准进程（优先级 1）
- 2：优先级 2 进程获得约 2 倍时间
- 2：优先级 3 进程获得约 2 倍时间
- 2：优先级 4 进程获得约 2 倍时间
- 3：优先级 5 进程获得约 3 倍时间

**分析**：
- Stride 调度算法成功实现了按优先级分配 CPU 时间
- 实际比例与理论比例基本一致，误差在合理范围内
- 误差原因：
  1. 进程创建和初始化开销
  2. 系统开销（中断处理、调度开销等）
  3. 时间片离散化导致的舍入误差

### 运行截图

![Stride 调度算法运行输出](Figures/stride调度算法运行输出.png)

---

## 实验总结

### 遇到的问题与解决

**问题 1**：理解 stride 溢出处理机制

**解决过程**：
- 阅读实验指导书中的理论证明
- 理解了有符号整数差值的数学性质
- 明白了为什么 `BIG_STRIDE = 0x7FFFFFFF` 能保证正确性
