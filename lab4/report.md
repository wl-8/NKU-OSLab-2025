# 2025 操作系统 Lab4
> 团队成员: 2313857陈天祺 & 2311208魏来 & 2312166王旭

## 实验目的
---

- 了解虚拟内存管理的基本结构，掌握虚拟内存的组织与管理方式
- 了解内核线程创建/执行的管理过程
- 了解内核线程的切换和基本调度过程

## 实验内容
---

### 练习0：填写已有实验

本实验依赖实验2/3。请把你做的实验2/3的代码填入本实验中代码中有“LAB2”,“LAB3”的注释相应部分。

### 练习1：分配并初始化一个进程控制块

`alloc_proc` 函数（位于 kern/process/proc.c ）负责分配并返回一个新的 `struct proc_struct` 结构，用于存储新建立的内核线程的管理信息。ucore需要对这个结构进行最基本的初始化，你需要完成这个初始化过程。

请在实验报告中简要说明你的设计实现过程，并回答如下问题：

**请说明 `proc_struct` 中 `struct context context` 和 `struct trapframe *tf` 成员变量含义和在本实验中的作用是啥？（==提示：看代码和编程调试可以判断出来==）**

### 练习2：

创建一个内核线程需要分配和设置好很多资源。`kernel_thread` 函数通过调用 `do_fork` 函数完成具体内核线程的创建工作。

`do_kernel`函数会调用 `alloc_proc` 函数来分配并初始化一个进程控制块，但 `alloc_proc` 只是找到了一小块内存用以记录进程的必要信息，并没有实际分配这些资源。ucore 一般通过 `do_fork` 实际创建新的内核线程。 

`do_fork` 的作用是，创建当前内核线程的一个副本，它们的执行上下文、代码、数据都一样，但是存储位置不同。因此，我们实际需要"fork"的东西就是 stack 和 trapframe。在这个过程中，需要给新内核线程分配资源，并且复制原进程的状态。你需要完成在 kern/process/proc.c 中的 `do_fork` 函数中的处理过程。它的大致执行步骤包括：

1. 调用alloc_proc，首先获得一块用户信息块。
2. 为进程分配一个内核栈。
3. 复制原进程的内存管理信息到新进程（但内核线程不必做此事）
4. 复制原进程上下文到新进程
5. 将新进程添加到进程列表
6. 唤醒新进程
7. 返回新进程号

请在实验报告中简要说明你的设计实现过程并回答如下问题：

**请说明 ucore 是否做到给每个新 fork 的线程一个唯一的 id？请说明你的分析和理由。**

### 练习3：

`proc_run` 用于将指定的进程切换到 CPU 上运行。它的大致执行步骤包括：

1. 检查要切换的进程是否与当前正在运行的进程相同，如果相同则不需要切换。
2. 禁用中断。你可以使用 /kern/sync/sync.h 中定义好的宏 `local_intr_save(x)` 和 `local_intr_restore(x)` 来实现关、开中断。
3. 切换当前进程为要运行的进程。
4. 切换页表，以便使用新进程的地址空间。/libs/riscv.h 中提供了 `lsatp`(unsigned int pgdir) 函数，可实现修改 SATP 寄存器值的功能。
5. 实现上下文切换。/kern/process 中已经预先编写好了 switch.S，其中定义了 `switch_to()` 函数。可实现两个进程的 context 切换。
6. 允许中断。

请回答如下问题：
**在本实验的执行过程中，创建且运行了几个内核线程？**

完成代码编写后，编译并运行代码：`make qemu`

### Challenge1：

1. 说明语句local_intr_save(intr_flag);....local_intr_restore(intr_flag);是如何实现开关中断的？

2. `get_pte()` 函数（位于kern/mm/pmm.c）用于在页表中查找或创建页表项，从而实现对指定线性地址对应的物理页的访问和映射操作。这在操作系统中的分页机制下，是实现虚拟内存与物理内存之间映射关系非常重要的内容。
    2.1 `get_pte()` 函数中有两段形式类似的代码， 结合sv32，sv39，sv48的异同，解释这两段代码为什么如此相像。
    2.2 目前 `get_pte()` 函数将页表项的查找和页表项的分配合并在一个函数里，你认为这种写法好吗？有没有必要把两个功能拆开？

## 实验过程
---

### 练习1



---
### 练习2

#### 2.1 实现代码

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

#### 2.2 设计实现过程

##### 2.2.1 总体流程图

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

##### 2.2.2 关键步骤详解

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

##### 2.2.3 错误处理机制：链式清理

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

#### 2.3 思考题：ucore 是否做到给每个新 fork 的线程一个唯一的 id？

**答案：是的，uCore 能够保证每个新 fork 的线程都有唯一的 PID。**

##### 2.3.1 get_pid() 函数分析

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

##### 2.3.2 算法原理

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

##### 2.3.4 唯一性保证

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

---
### 练习3

#### 3.1 实现流程

1. 调用宏 `local_intr_save` 禁用中断
```c
    bool intr_flag;     // 中断状态
    local_intr_save(intr_flag); 
```
2. 切换当前运行进程
```c
    struct proc_struct *prev = current;  // 保存当前进程指针
    current = proc;                      // 更新当前进程指针 
```
3. 切换页表以使用新进程的地址空间（通过修改 `satp` 寄存器值）
```c
lsatp(proc->pgdir);                  // 切换页表 - 修改 satp 寄存器的值
```
4. 切换进程上下文
```c
switch_to(&(prev->context), &(proc->context));  // 进程上下文切换
```
5. 恢复中断状态
```c
local_intr_restore(intr_flag);       // 恢复中断状态
```

#### 3.2 关于内核线程

本实验中创建并运行了 **2 个** 内核线程，分别是空闲进程 `idleproc` 和初始进程 `initproc`。具体流程如下：

##### 3.2.1 系统初始化

系统内核启动后，在 `kern_init` 函数中调用 `proc_init` 初始化进程管理系统。

##### 3.2.2 进程管理系统初始化

1. 创建空闲进程
```c
// 创建空闲进程
if ((idleproc = alloc_proc()) == NULL) {
    panic("cannot alloc idleproc.\n");
}
// 设置空闲进程属性
idleproc->pid = 0;
idleproc->state = PROC_RUNNABLE;
idleproc->kstack = (uintptr_t)bootstack;
idleproc->need_resched = 1;
set_proc_name(idleproc, "idle");
current = idleproc;  // 设为当前进程
```

2. 创建初始进程
```c
// 通过 kernel_thread 创建初始进程
int pid = kernel_thread(init_main, "Hello world!!", 0);
initproc = find_proc(pid);
set_proc_name(initproc, "init");
```

##### 3.2.3 内核线程创建

1. 调用 `kernel_thread()` 函数

2. 设置陷阱帧 `trapframe`

3. 调用 `do_fork()` 创建进程

##### 3.2.4 进程运行

1. 进入 `cpu_idle` 开始进程调度

2. 进程切换
    - `schedule()` 选择要运行的进程
    - `proc_run()` 执行进程切换
    - `switch_to()` 切换进程上下文

3. 内核线程(初始进程)执行
    - 跳转内核入口点 `forkret()`
    - 再跳转 `init_main` 执行并打印消息

---

#### 3.3 程序运行

```bash
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
DTB Init
HartID: 0
DTB Address: 0x82200000
Physical Memory from DTB:
  Base: 0x0000000080000000
  Size: 0x0000000008000000 (128 MB)
  End:  0x0000000087ffffff
DTB init completed
(THU.CST) os is loading ...

Special kernel symbols:
  entry  0xc020004a (virtual)
  etext  0xc0203e4e (virtual)
  edata  0xc0209030 (virtual)
  end    0xc020d4f0 (virtual)
Kernel executable memory footprint: 54KB
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
++ setup timer interrupts
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
kernel panic at kern/process/proc.c:457:
    process exit!!.

Welcome to the kernel debug monitor!!
Type 'help' for a list of commands.
```

### Challenge1

#### 问题1：说明语句 `local_intr_save(intr_flag); ... local_intr_restore(intr_flag);` 是如何实现开关中断的

##### 1. 实现代码

位于 `kern/sync/sync.h`：

```c
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();  // 关闭中断
        return 1;
    }
    return 0;
}

static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();  // 恢复中断
    }
}

#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)

#define local_intr_restore(x) __intr_restore(x);
```

##### 2. 工作原理

**步骤1：保存中断状态并关闭中断**
```c
bool intr_flag;
local_intr_save(intr_flag);
```

展开后：
```c
intr_flag = __intr_save();
```

`__intr_save()` 的逻辑：
```c
if (read_csr(sstatus) & SSTATUS_SIE) {
    // SIE位为1，说明中断当前是开启的
    intr_disable();  // 关闭中断（清除SIE位）
    return 1;        // 返回1表示之前中断是开启的
}
return 0;  // 返回0表示之前中断就是关闭的
```

**步骤2：执行临界区代码**
```c
{
    // 临界区代码
    // 此时中断已关闭，不会被打断
}
```

**步骤3：恢复中断状态**
```c
local_intr_restore(intr_flag);
```

展开后：
```c
__intr_restore(intr_flag);
```

`__intr_restore()` 的逻辑：
```c
if (flag) {
    // flag=1，说明之前中断是开启的
    intr_enable();  // 重新开启中断（设置SIE位）
}
// flag=0，说明之前中断就是关闭的，保持关闭
```

##### 3. 底层实现

**intr_disable() 和 intr_enable()**：

```c
void intr_enable(void) {
    set_csr(sstatus, SSTATUS_SIE);  // 设置SIE位
}

void intr_disable(void) {
    clear_csr(sstatus, SSTATUS_SIE);  // 清除SIE位
}
```

**RISC-V 的 sstatus 寄存器**：
- `SIE` (Supervisor Interrupt Enable) 位控制中断开关
- SIE=1：中断开启
- SIE=0：中断关闭

##### 4. 为什么不直接 disable/enable？

**错误的做法**：
```c
intr_disable();  // 关中断
{
    // 临界区
}
intr_enable();  // 开中断
```

**问题**：
```
假设调用前中断就是关闭的：
  intr_disable()  → 中断关闭（OK）
  临界区...
  intr_enable()   → 中断开启（错误！）

结果：改变了原来的中断状态！
```

**正确的做法**：
```c
local_intr_save(intr_flag);  // 保存状态并关中断
{
    // 临界区
}
local_intr_restore(intr_flag);  // 恢复原来的状态
```

**保证**：退出临界区后，中断状态与进入前完全一致。

##### 5. 应用场景

在 `do_fork` 中：
```c
bool intr_flag;
local_intr_save(intr_flag);
{
    proc->pid = get_pid();              // 原子操作1
    hash_proc(proc);                    // 原子操作2
    list_add(&proc_list, &(proc->list_link));  // 原子操作3
    nr_process++;                       // 原子操作4
}
local_intr_restore(intr_flag);
```

如果不关中断：
```
执行到一半 → 时钟中断 → 调度器查看proc_list
→ 看到不完整的进程 → 崩溃
```


#### 问题2：深入理解 `get_pte()` 函数（位于kern/mm/pmm.c）

##### 子问题1： `get_pte()` 函数中有两段形式类似的代码， 结合sv32，sv39，sv48的异同，解释这两段代码为什么如此相像。

`get_pte()` 中有相似代码：

1. 处理一级页目录
```
pde_t *pdep1 = &pgdir[PDX1(la)];
if (!(*pdep1 & PTE_V)) {
    struct Page *page;
    if (!create || (page = alloc_page()) == NULL) {
        return NULL;
    }
    set_page_ref(page, 1);
    uintptr_t pa = page2pa(page);
    memset(KADDR(pa), 0, PGSIZE);
    *pdep1 = pte_create(page2ppn(page), PTE_U | PTE_V);
}
```

2. 处理二级页目录
```
pde_t *pdep0 = &((pte_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)];
if (!(*pdep0 & PTE_V)) {
    struct Page *page;
    if (!create || (page = alloc_page()) == NULL) {
        return NULL;
    }
    set_page_ref(page, 1);
    uintptr_t pa = page2pa(page);
    memset(KADDR(pa), 0, PGSIZE);
    *pdep0 = pte_create(page2ppn(page), PTE_U | PTE_V);
}
```

这两段代码相似是因为 RISC-V 多级页表的层次化结构：

1. **sv32**（32位虚拟地址）：采用 2 级页表
    - 第1级：页目录表 (Page Directory)
    - 第2级：页表 (Page Table)
2. **sv39**（39位虚拟地址）：采用 3 级页表
    - 第1级：页全局目录 (Page Global Directory)
    - 第2级：页目录表 (Page Directory)
    - 第3级：页表 (Page Table)
3. **sv48**（48位虚拟地址）：采用 4 级页表
    - 增加更多层次

每一级页表的操作逻辑完全相同：

- 检查当前级别的页表项是否有效 (`PTE_V` 标志)
- 如果无效且需要创建，则分配新页面
- 初始化页面内容为 0
- 设置页表项指向新分配的页面

这种统一的层次化设计使得不同级别的页表操作完全一致，对应代码也就风格相似。

##### 子问题2：目前 `get_pte()` 函数将页表项的查找和页表项的分配合并在一个函数里，你认为这种写法好吗？有没有必要把两个功能拆开？

当前设计的优点：

1. **接口简洁**：一个函数完成查找和创建两种功能，调用方便
2. **原子性操作**：避免了查找和创建之间的竞争条件
3. **减少重复遍历**：不需要两次遍历页表层次结构

当前设计的缺点：

1. **功能耦合**：违反了单一职责原则
2. **参数语义模糊**：`create` 参数使函数行为不够明确
3. **错误处理复杂**：需要在一个函数中处理多种失败情况
4. **代码可读性**：函数逻辑相对复杂

可以选择把两种处理逻辑拆分如下：

```
// 页表项查找函数
pte_t *find_pte(pde_t *pgdir, uintptr_t la);

// 页表项创建函数  
pte_t *create_pte(pde_t *pgdir, uintptr_t la);

// 组合函数（保持兼容性）
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create) {
    pte_t *pte = find_pte(pgdir, la);
    if (pte == NULL && create) {
        pte = create_pte(pgdir, la);
    }
    return pte;
}
```


