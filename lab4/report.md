# Lab4：进程管理

## 练习2：为新创建的内核线程分配资源（2311208）

### 2.1 实现代码

`do_fork` 函数是创建新进程的核心函数，位于 `kern/process/proc.c`：

```c
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS)
    {
        goto fork_out;
    }
    ret = -E_NO_MEM;

    //    1. call alloc_proc to allocate a proc_struct
    proc = alloc_proc();
    if (proc == NULL)
    {
        goto fork_out; // 分配proc_struct失败，直接返回
    }

    //    2. call setup_kstack to allocate a kernel stack for child process
    if (setup_kstack(proc) != 0)
    {
        goto bad_fork_cleanup_proc; // 分配内核栈失败，清理proc_struct后返回
    }

    //    3. call copy_mm to dup OR share mm according clone_flag
    if (copy_mm(clone_flags, proc) != 0)
    {
        goto bad_fork_cleanup_kstack; // 复制或共享内存管理结构失败，清理内核栈和proc_struct后返回
    }

    //    4. call copy_thread to setup tf & context in proc_struct
    copy_thread(proc, stack, tf);

    //    5. insert proc_struct into hash_list && proc_list
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        proc->pid = get_pid();
        hash_proc(proc);
        list_add(&proc_list, &(proc->list_link));
        nr_process++;
    }
    local_intr_restore(intr_flag);

    //    6. call wakeup_proc to make the new child process RUNNABLE
    wakeup_proc(proc);

    //    7. set ret vaule using child proc's pid
    ret = proc->pid;

fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc); // 释放内核栈之后会继续向下清理proc_struct
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}
```

### 2.2 设计实现过程

#### 2.2.1 总体流程图

```
do_fork开始
    ↓
检查进程数是否超限
    ↓
[1] alloc_proc() 分配进程控制块
    ↓ 失败？→ 返回错误
    ↓
[2] setup_kstack() 分配内核栈
    ↓ 失败？→ 清理proc → 返回错误
    ↓
[3] copy_mm() 复制/共享内存管理信息
    ↓ 失败？→ 清理kstack和proc → 返回错误
    ↓
[4] copy_thread() 设置中断帧和上下文
    ↓
[5] 关中断
    ↓ 分配PID
    ↓ 加入哈希表
    ↓ 加入进程链表
    ↓ 进程数+1
    ↓ 开中断
    ↓
[6] wakeup_proc() 设置为RUNNABLE
    ↓
[7] 返回新进程的PID
```

#### 2.2.2 关键步骤详解

**步骤1：分配进程控制块**
```c
proc = alloc_proc();
if (proc == NULL) {
    goto fork_out;  // 内存不足，直接返回
}
```

**步骤2：分配内核栈**
```c
if (setup_kstack(proc) != 0) {
    goto bad_fork_cleanup_proc;  // 清理已分配的proc
}
```
- 每个进程需要独立的内核栈
- 大小为`KSTACKPAGE`个页面（通常8KB）
- 用于保存中断帧和函数调用栈

**步骤3：复制内存管理信息**
```c
if (copy_mm(clone_flags, proc) != 0) {
    goto bad_fork_cleanup_kstack;  // 清理kstack和proc
}
```
- 在LAB4中，内核线程的mm为NULL，此函数直接返回0
- 后续实验中会实现用户进程的内存复制

**步骤4：设置中断帧和上下文**
```c
copy_thread(proc, stack, tf);
```

详细分析：
```c
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    // 在内核栈顶部预留中断帧空间
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe));
    *(proc->tf) = *tf;  // 复制临时中断帧到进程内核栈

    proc->tf->gpr.a0 = 0;  // 子进程返回值设为0
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;

    proc->context.ra = (uintptr_t)forkret;  // 第一次调度时的返回地址
    proc->context.sp = (uintptr_t)(proc->tf);  // 上下文栈指针指向中断帧
}
```

**内核栈布局**：
```
高地址 (kstack + KSTACKSIZE)
    +------------------+
    |   trapframe      |  ← proc->tf, context.sp
    +------------------+
    |                  |
    |   可用栈空间     |
    |   (向下增长)     |
    |                  |
    +------------------+
低地址 (kstack)
```

**步骤5：加入进程管理数据结构**
```c
bool intr_flag;
local_intr_save(intr_flag);  // 关中断，保证原子性
{
    proc->pid = get_pid();              // 分配唯一PID
    hash_proc(proc);                    // 加入哈希表（根据PID快速查找）
    list_add(&proc_list, &(proc->list_link));  // 加入进程链表（顺序遍历）
    nr_process++;                       // 进程计数+1
}
local_intr_restore(intr_flag);  // 恢复中断
```

**为什么要关中断**：
1. 这4个操作必须原子执行
2. 防止中断处理程序看到不一致的状态
3. 防止多核CPU的竞争条件

**步骤6：唤醒新进程**
```c
wakeup_proc(proc);
// 实现：proc->state = PROC_RUNNABLE;
```

**步骤7：返回新进程PID**
```c
ret = proc->pid;  // 父进程得到子进程的PID
```

#### 2.2.3 错误处理机制：链式清理

```c
bad_fork_cleanup_kstack:
    put_kstack(proc);     // 释放内核栈
bad_fork_cleanup_proc:
    kfree(proc);          // 释放进程控制块
    goto fork_out;
```

**设计巧妙之处**：
- 利用C语言的fall-through特性
- 标签之间没有break，会顺序执行
- 实现逆序清理（后分配的先释放）

**三种失败情况**：
1. **步骤2失败**：跳到`bad_fork_cleanup_proc`，只释放proc
2. **步骤3失败**：跳到`bad_fork_cleanup_kstack`，先释放kstack，再释放proc
3. **步骤1失败**：直接跳到`fork_out`，什么都不清理

### 2.3 思考题：ucore 是否做到给每个新 fork 的线程一个唯一的 id？

**答案：是的，uCore 能够保证每个新 fork 的线程都有唯一的 PID。**

#### 2.3.1 get_pid() 函数分析

```c
static int get_pid(void) {
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;

    if (++last_pid >= MAX_PID) {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe) {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list) {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid) {
                if (++last_pid >= next_safe) {
                    if (last_pid >= MAX_PID) {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid) {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}
```

#### 2.3.2 算法原理

**核心思想**：使用静态变量 `last_pid` 记录上次分配的PID，每次分配时递增并检查冲突。

**关键变量**：
- `last_pid`：上次分配的PID
- `next_safe`：下一个"安全边界"，在`[last_pid, next_safe)`范围内没有已分配的PID

**工作流程**：

1. **快速路径**（常见情况）：
```c
if (++last_pid >= MAX_PID) {
    last_pid = 1;  // PID用完了，从1重新开始
    goto inside;
}
if (last_pid >= next_safe) {
    // last_pid超出安全范围，需要重新检查
    goto inside;
}
return last_pid;  // 在安全范围内，直接返回
```

2. **慢速路径**（需要遍历检查）：
```c
inside:
    next_safe = MAX_PID;
repeat:
    le = list;
    while ((le = list_next(le)) != list) {
        proc = le2proc(le, list_link);
        if (proc->pid == last_pid) {
            // 冲突！递增last_pid
            if (++last_pid >= next_safe) {
                // 超出安全范围，需要重新扫描
                if (last_pid >= MAX_PID) {
                    last_pid = 1;  // PID回绕到1
                }
                next_safe = MAX_PID;  // 重置安全边界
                goto repeat;  // 重新扫描所有进程
            }
        }
        else if (proc->pid > last_pid && next_safe > proc->pid) {
            // 找到比当前next_safe更小的已占用PID
            // 更新next_safe，缩小安全区间
            next_safe = proc->pid;
        }
    }
    return last_pid;  // 找到了可用的PID
```

#### 2.3.3 算法核心机制

**安全区间 `[last_pid, next_safe)` 的含义**：

- **`last_pid`**: 下一个候选PID
- **`next_safe`**: 第一个已知会冲突的PID
- **安全区间**: `[last_pid, next_safe)` 范围内的PID都是未被占用的

**关键逻辑**：

1. **`if (last_pid >= next_safe)`**:
   - 含义：安全区间已用完，必须重新扫描所有进程
   - 触发：当连续分配PID或发生冲突时，`last_pid` 递增到 `next_safe`

2. **`if (proc->pid == last_pid)`**:
   - 含义：当前候选PID已被占用
   - 处理：递增 `last_pid`，并检查是否超出安全区间

3. **`else if (proc->pid > last_pid && next_safe > proc->pid)`**:
   - 含义：找到一个比当前 `next_safe` 更小的已占用PID
   - 作用：缩小安全区间，优化下次分配

**时间复杂度**：
- **最优情况**：O(1) - 快速路径，无冲突
- **最坏情况**：O(n) - 慢速路径，需要遍历所有进程
- **平均情况**：接近O(1) - 安全区间机制减少了重复扫描

#### 2.3.4 唯一性保证

**三重保证机制**：

1. **遍历检查**：
   - 每次分配都检查是否与现有PID冲突
   - 有冲突就递增并重新检查

2. **原子操作**：
   ```c
   bool intr_flag;
   local_intr_save(intr_flag);  // 关中断
   {
       proc->pid = get_pid();   // 分配PID
       hash_proc(proc);         // 立即加入哈希表
       list_add(&proc_list, &(proc->list_link));  // 加入链表
   }
   local_intr_restore(intr_flag);  // 开中断
   ```
   - 分配PID和加入链表是原子操作
   - 防止两个进程同时分配到相同PID

3. **范围限制**：
   ```c
   static_assert(MAX_PID > MAX_PROCESS);
   ```
   - PID空间大于进程数上限
   - 保证总能找到可用的PID

**结论**：uCore 的 PID 分配机制能够保证每个新 fork 的线程都有唯一的 ID。