// 包含必要的头文件
#include <proc.h>      // 进程管理相关定义
#include <kmalloc.h>   // 内核内存分配
#include <string.h>    // 字符串操作函数
#include <sync.h>      // 同步原语
#include <pmm.h>       // 物理内存管理
#include <error.h>     // 错误码定义
#include <sched.h>     // 调度相关
#include <elf.h>       // ELF 格式支持
#include <vmm.h>       // 虚拟内存管理
#include <trap.h>      // 陷阱和中断处理
#include <stdio.h>     // 标准 I/O 函数
#include <stdlib.h>    // 标准库函数
#include <assert.h>    // 断言宏

/* ------------- process/thread mechanism design&implementation -------------
 * 进程/线程机制的设计与实现（简化的 Linux 进程/线程机制）
 * 
 * 简介：
 *   uCore 实现了一个简单的进程/线程机制。进程包含独立的内存空间、至少一个用于执行的线程、
 *   内核数据（用于管理）、处理器状态（用于上下文切换）、文件（在 lab6 中）等。
 *   uCore 需要高效地管理所有这些细节。在 uCore 中，线程只是一种特殊的进程（共享进程的内存）。
 * 
 * ------------------------------
 * process state       :     meaning               -- reason
 * 进程状态         :     含义                  -- 发生原因
 *     PROC_UNINIT     :   uninitialized           -- alloc_proc
 *                     :   未初始化              -- 分配进程
 *     PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
 *                     :   睡眠                   -- 尝试释放页面、等待、睡眠
 *     PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc,
 *                     :   可运行（可能正在运行） -- 进程初始化、唤醒进程
 *     PROC_ZOMBIE     :   almost dead             -- do_exit
 *                     :   僵尸状态               -- 进程退出
 * 
 * -----------------------------
 * process state changing:
 * 进程状态转换：
 * 
 *   alloc_proc                                 RUNNING
 *       +                                   +--<----<--+
 *       +                                   + proc_run +
 *       V                                   +-->---->--+
 * PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
 *                                            A      +                                                           +
 *                                            |      +--- do_exit --> PROC_ZOMBIE                                +
 *                                            +                                                                  +
 *                                            -----------------------wakeup_proc----------------------------------
 * 
 * -----------------------------
 * process relations
 * 进程关系：
 * parent:           proc->parent  (proc is children) - 父进程
 * children:         proc->cptr    (proc is parent) - 子进程
 * older sibling:    proc->optr    (proc is younger sibling) - 兄弟进程
 * younger sibling:  proc->yptr    (proc is older sibling) - 弟弟进程
 * 
 * -----------------------------
 * related syscall for process:
 * 与进程相关的系统调用：
 * SYS_exit        : process exit,                           -->do_exit
 *                 : 进程退出                              -->调用 do_exit
 * SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
 *                 : 创建子进程，复制内存管理结构    -->调用 do_fork-->唤醒进程
 * SYS_wait        : wait process                            -->do_wait
 *                 : 等待进程                             -->调用 do_wait
 * SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
 *                 : fork 后，进程执行程序                -->加载程序并刷新内存管理
 * SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
 *                 : 创建子线程                          -->调用 do_fork-->唤醒进程
 * SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
 *                 : 进程标记自身需要重新调度       -- proc->need_sched=1，然后调度器会重新调度这个进程
 * SYS_sleep       : process sleep                           -->do_sleep
 *                 : 进程睡眠                            -->调用 do_sleep
 * SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
 *                 : 杀死进程                             -->do_kill-->proc->flags |= PF_EXITING
 *                                                                   -->wakeup_proc-->do_wait-->do_exit
 *                                                                   -->唤醒进程-->等待-->退出
 * SYS_getpid      : get the process's pid
 *                 : 获取进程的 PID
 * 
 */

// the process set's list
// 进程集合的链表
list_entry_t proc_list;

// 哈希表相关定义
#define HASH_SHIFT 10                        // 哈希表的位移数
#define HASH_LIST_SIZE (1 << HASH_SHIFT)     // 哈希表大小（1024）
#define pid_hashfn(x) (hash32(x, HASH_SHIFT)) // PID 哈希函数，用于快速查找进程

// has list for process set based on pid
// 基于 PID 的进程集合哈希表
static list_entry_t hash_list[HASH_LIST_SIZE];

// 关键进程指针
// idle proc
struct proc_struct *idleproc = NULL;  // 空闲进程（系统空闲时运行）
// init proc
struct proc_struct *initproc = NULL;  // 初始进程（第一个用户进程）
// current proc
struct proc_struct *current = NULL;   // 当前正在运行的进程

static int nr_process = 0;  // 当前系统中的进程数量

// 函数声明
void kernel_thread_entry(void);                           // 内核线程入口函数
void forkrets(struct trapframe *tf);                      // fork 返回函数
void switch_to(struct context *from, struct context *to); // 进程上下文切换函数

// alloc_proc - alloc a proc_struct and init all fields of proc_struct
// 分配进程控制块 - 分配一个 proc_struct 并初始化所有字段
static struct proc_struct *
alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));  // 分配进程控制块内存
    if (proc != NULL)
    {
        // LAB4:EXERCISE1 2312166
        // TODO LAB4:练习1 你的代码
        /*
         * below fields in proc_struct need to be initialized
         * 下面 proc_struct 中的字段需要被初始化：
         *       enum proc_state state;                      // Process state - 进程状态
         *       int pid;                                    // Process ID - 进程 ID
         *       int runs;                                   // the running times of Proces - 进程运行次数
         *       uintptr_t kstack;                           // Process kernel stack - 进程内核栈
         *       volatile bool need_resched;                 // bool value: need to be rescheduled to release CPU?
         *                                                   // 是否需要重新调度以释放 CPU
         *       struct proc_struct *parent;                 // the parent process - 父进程
         *       struct mm_struct *mm;                       // Process's memory management field - 进程内存管理字段
         *       struct context context;                     // Switch here to run process - 在此切换以运行进程
         *       struct trapframe *tf;                       // Trap frame for current interrupt - 当前中断的陷阱帧
         *       uintptr_t pgdir;                            // the base addr of Page Directroy Table(PDT)
         *                                                   // 页目录表的基地址
         *       uint32_t flags;                             // Process flag - 进程标志
         *       char name[PROC_NAME_LEN + 1];               // Process name - 进程名称
         */
        proc->state = PROC_UNINIT;      // 初始状态为未初始化uninitialized,表示PCB已分配但未就绪
        proc->pid = -1;                 // PID未分配，设为 -1
        proc->runs = 0;                 // 运行次数初始化为0
        proc->kstack = 0;               // 内核栈未分配,内核栈基地址设为0
        proc->need_resched = 0;         // 不需要重新调度
        proc->parent = NULL;            // 无父进程
        proc->mm = NULL;                // 内核线程共享内核地址空间,无内存管理结构体,为NULL
        memset(&(proc->context), 0, sizeof(struct context)); // 上下文结构体清零
        proc->tf = NULL;                // trapframe指针初始化为NULL
        proc->pgdir = boot_pgdir_pa;    // 页目录表基地址初始化为boot_pgdir_pa(物理地址,因为后续进程转换时涉及到存放入satp中,而satp所需要的是PPN物理页号),使用页表基址(内核线程共享)
        proc->flags = 0;                // 进程标志初始化为0
        memset(proc->name, 0, sizeof(proc->name)); // 进程名清零
    }
    return proc;  // 返回分配的进程控制块指针（可能为 NULL）
}

// set_proc_name - set the name of proc
// 设置进程名称 - 为进程设置名称
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));  // 清空进程名称缓冲区
    return memcpy(proc->name, name, PROC_NAME_LEN);  // 复制新名称到进程控制块
}

// get_proc_name - get the name of proc
// 获取进程名称 - 从进程控制块获取进程名称
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];  // 静态名称缓冲区
    memset(name, 0, sizeof(name));  // 清空缓冲区
    return memcpy(name, proc->name, PROC_NAME_LEN);  // 复制进程名称到缓冲区
}

// get_pid - alloc a unique pid for process
// 获取 PID - 为进程分配一个唯一的 PID
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);  // 确保 PID 最大值大于最大进程数
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;  // 静态变量：下一个安全 PID 和上一个分配的 PID
    if (++last_pid >= MAX_PID)  // 如果上次分配的 PID 大于等于最大值
    {
        last_pid = 1;  // 从 1 开始重新分配（PID 0 留给内核）
        goto inside;
    }
    if (last_pid >= next_safe)  // 如果达到了下一个不安全的 PID
    {
    inside:
        next_safe = MAX_PID;  // 重置下一个安全 PID
    repeat:
        le = list;  // 遍历进程列表
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)  // 如果发现 PID 冲突
            {
                if (++last_pid >= next_safe)  // 递增 PID 并检查是否超出安全范围
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;  // 超出最大值则从 1 开始
                    }
                    next_safe = MAX_PID;
                    goto repeat;  // 重新搜索
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;  // 更新下一个不安全的 PID
            }
        }
    }
    return last_pid;  // 返回分配的唯一 PID
}

// proc_run - make process "proc" running on cpu
// 运行进程 - 使进程 "proc" 在 CPU 上运行
// NOTE: before call switch_to, should load  base addr of "proc"'s new PDT
// 注意：在调用 switch_to 之前，应该加载 "proc" 的新页目录表的基地址
void proc_run(struct proc_struct *proc)
{
    if (proc != current)  // 如果要运行的进程不是当前进程
    {
        // LAB4:EXERCISE3 YOUR CODE
        // TODO LAB4:练习3 2313857
        /*
         * Some Useful MACROs, Functions and DEFINEs, you can use them in below implementation.
         * 一些有用的宏、函数和定义，你可以在下面的实现中使用它们。
         * MACROs or Functions:
         * 宏或函数：
         *   local_intr_save():        Disable interrupts - 禁用中断
         *   local_intr_restore():     Enable Interrupts - 启用中断
         *   lsatp():                   Modify the value of satp register - 修改 satp 寄存器的值
         *   switch_to():              Context switching between two processes - 两个进程间的上下文切换
         */
        bool intr_flag;
        local_intr_save(intr_flag);          // 禁用中断并保存中断状态

        struct proc_struct *prev = current;  // 保存当前进程指针
        current = proc;                      // 更新当前进程指针

        lsatp(proc->pgdir);                  // 切换页表 - 修改 satp 寄存器的值

        switch_to(&(prev->context), &(proc->context));   // 进程上下文切换

        local_intr_restore(intr_flag);       // 恢复中断状态
    }
}

// forkret -- the first kernel entry point of a new thread/process
// forkret -- 新线程/进程的第一个内核入口点
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
// 注意：forkret 的地址在 copy_thread 函数中设置
//      在 switch_to 之后，当前进程将在这里执行。
static void
forkret(void)
{
    forkrets(current->tf);  // 调用 forkrets 函数，传入当前进程的陷阱帧
}

// hash_proc - add proc into proc hash_list
// 哈希进程 - 将进程添加到进程哈希表中
static void
hash_proc(struct proc_struct *proc)
{
    // 根据 PID 计算哈希值，将进程添加到对应的哈希链表中
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
// 查找进程 - 根据 PID 从进程哈希表中查找进程
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)  // 检查 PID 的有效性
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;  // 获取对应的哈希链表
        while ((le = list_next(le)) != list)  // 遍历哈希链表
        {
            struct proc_struct *proc = le2proc(le, hash_link);  // 从链表节点获取进程控制块
            if (proc->pid == pid)  // 如果找到匹配的 PID
            {
                return proc;  // 返回进程控制块
            }
        }
    }
    return NULL;  // 未找到进程，返回 NULL
}

// kernel_thread - create a kernel thread using "fn" function
// 内核线程 - 使用 "fn" 函数创建内核线程
// NOTE: the contents of temp trapframe tf will be copied to
//       proc->tf in do_fork-->copy_thread function
// 注意：临时陷阱帧 tf 的内容将在 do_fork-->copy_thread 函数中被复制到 proc->tf
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;  // 临时陷阱帧
    memset(&tf, 0, sizeof(struct trapframe));  // 初始化陷阱帧为 0
    tf.gpr.s0 = (uintptr_t)fn;   // s0 寄存器保存线程主函数地址
    tf.gpr.s1 = (uintptr_t)arg;  // s1 寄存器保存线程参数
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;  // 设置状态寄存器：内核模式，允许中断
    tf.epc = (uintptr_t)kernel_thread_entry;  // 设置程序计数器为内核线程入口
    return do_fork(clone_flags | CLONE_VM, 0, &tf);  // 调用 do_fork 创建进程，共享虚拟内存
}

// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
// 设置内核栈 - 分配 KSTACKPAGE 大小的页面作为进程内核栈
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);  // 分配内核栈页面
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);  // 将页面地址转换为虚拟地址并保存
        return 0;  // 成功
    }
    return -E_NO_MEM;  // 内存不足
}

// put_kstack - free the memory space of process kernel stack
// 释放内核栈 - 释放进程内核栈的内存空间
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);  // 将虚拟地址转换为页面并释放
}

// copy_mm - process "proc" duplicate OR share process "current"'s mm according clone_flags
//         - if clone_flags & CLONE_VM, then "share" ; else "duplicate"
// 复制内存管理 - 根据 clone_flags，进程 "proc" 复制或共享当前进程的内存管理结构
//                - 如果 clone_flags & CLONE_VM，则 "共享"；否则 "复制"
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    assert(current->mm == NULL);  // 当前实验中，所有进程都是内核进程，mm 为 NULL
    /* do nothing in this project */
    /* 在这个实验中不做任何事情 */
    return 0;  // 成功返回
}

// copy_thread - setup the trapframe on the  process's kernel stack top and
//             - setup the kernel entry point and stack of process
// 复制线程 - 在进程内核栈顶部设置陷阱帧，并设置进程的内核入口点和栈
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    // 在内核栈顶部设置陷阱帧位置
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe));
    *(proc->tf) = *tf;  // 复制陷阱帧内容

    // Set a0 to 0 so a child process knows it's just forked
    // 将 a0 设置为 0，这样子进程就知道自己刚被 fork
    proc->tf->gpr.a0 = 0;
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;  // 设置栈指针

    proc->context.ra = (uintptr_t)forkret;  // 设置返回地址为 forkret 函数
    proc->context.sp = (uintptr_t)(proc->tf);  // 设置上下文栈指针指向陷阱帧
}

/* do_fork -     parent process for a new child process
 * do_fork -     父进程为新子进程创建进程
 * @clone_flags: used to guide how to clone the child process
 *               用于指导如何克隆子进程
 * @stack:       the parent's user stack pointer. if stack==0, It means to fork a kernel thread.
 *               父进程的用户栈指针。如果 stack==0，表示 fork 一个内核线程。
 * @tf:          the trapframe info, which will be copied to child process's proc->tf
 *               陷阱帧信息，将被复制到子进程的 proc->tf
 */
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    int ret = -E_NO_FREE_PROC;  // 默认返回值：没有空闲进程
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS)  // 检查进程数量是否超出限制
    {
        goto fork_out;  // 超出限制，直接返回
    }
    ret = -E_NO_MEM;  // 设置返回值：内存不足
    // LAB4:EXERCISE2 2311208
    // TODO LAB4:练习2 2311208
    /*
     * Some Useful MACwROs, Functions and DEFINEs, you can use them in below implementation.
     * 一些有用的宏、函数和定义，你可以在下面的实现中使用它们。
     * MACROs or Functions:
     * 宏或函数：
     *   alloc_proc:   create a proc struct and init fields (lab4:exercise1)
     *                 创建一个 proc 结构并初始化字段 (lab4:练习1)
     *   setup_kstack: alloc pages with size KSTACKPAGE as process kernel stack
     *                 分配 KSTACKPAGE 大小的页面作为进程内核栈
     *   copy_mm:      process "proc" duplicate OR share process "current"'s mm according clone_flags
     *                 根据 clone_flags，进程 "proc" 复制或共享当前进程的 mm
     *                 if clone_flags & CLONE_VM, then "share" ; else "duplicate"
     *                 如果 clone_flags & CLONE_VM，则 "共享"；否则 "复制"
     *   copy_thread:  setup the trapframe on the  process's kernel stack top and
     *                 在进程内核栈顶部设置陷阱帧并
     *                 setup the kernel entry point and stack of process
     *                 设置进程的内核入口点和栈
     *   hash_proc:    add proc into proc hash_list
     *                 将进程添加到进程哈希表中
     *   get_pid:      alloc a unique pid for process
     *                 为进程分配一个唯一的 pid
     *   wakeup_proc:  set proc->state = PROC_RUNNABLE
     *                 设置 proc->state = PROC_RUNNABLE
     * VARIABLES:
     * 变量：
     *   proc_list:    the process set's list
     *                 进程集合的列表
     *   nr_process:   the number of process set
     *                 进程集合的数量
     */

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

// do_exit - called by sys_exit
// do_exit - 由 sys_exit 调用
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//      调用 exit_mmap & put_pgdir & mm_destroy 来释放进程的几乎所有内存空间
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//      将进程状态设置为 PROC_ZOMBIE，然后调用 wakeup_proc(parent) 要求父进程回收自身。
//   3. call scheduler to switch to other process
//      调用调度器切换到其他进程
int do_exit(int error_code)
{
    panic("process exit!!.\n");  // 在这个实验中，进程退出会导致系统 panic
}

// init_main - the second kernel thread used to create user_main kernel threads
// init_main - 第二个内核线程，用于创建 user_main 内核线程
static int
init_main(void *arg)
{
    cprintf("this initproc, pid = %d, name = \"%s\"\n", current->pid, get_proc_name(current));  // 打印初始进程信息
    cprintf("To U: \"%s\".\n", (const char *)arg);  // 打印传入的参数
    cprintf("To U: \"en.., Bye, Bye. :)\"\n");  // 打印告别信息
    return 0;  // 返回 0 表示成功
}

// proc_init - set up the first kernel thread idleproc "idle" by itself and
//           - create the second kernel thread init_main
// 进程初始化 - 设置第一个内核线程 idleproc "空闲" 并创建第二个内核线程 init_main
void proc_init(void)
{
    int i;

    list_init(&proc_list);  // 初始化进程列表
    for (i = 0; i < HASH_LIST_SIZE; i++)  // 初始化所有哈希表项
    {
        list_init(hash_list + i);
    }

    if ((idleproc = alloc_proc()) == NULL)  // 分配空闲进程的进程控制块
    {
        panic("cannot alloc idleproc.\n");  // 分配失败则系统崩溃
    }

    // check the proc structure
    // 检查进程结构（验证 alloc_proc 函数的正确性）
    int *context_mem = (int *)kmalloc(sizeof(struct context));  // 分配上下文内存用于比较
    memset(context_mem, 0, sizeof(struct context));
    int context_init_flag = memcmp(&(idleproc->context), context_mem, sizeof(struct context));

    int *proc_name_mem = (int *)kmalloc(PROC_NAME_LEN);  // 分配进程名内存用于比较
    memset(proc_name_mem, 0, PROC_NAME_LEN);
    int proc_name_flag = memcmp(&(idleproc->name), proc_name_mem, PROC_NAME_LEN);

    // 验证 alloc_proc 函数是否正确初始化了所有字段
    if (idleproc->pgdir == boot_pgdir_pa && idleproc->tf == NULL && !context_init_flag && idleproc->state == PROC_UNINIT && idleproc->pid == -1 && idleproc->runs == 0 && idleproc->kstack == 0 && idleproc->need_resched == 0 && idleproc->parent == NULL && idleproc->mm == NULL && idleproc->flags == 0 && !proc_name_flag)
    {
        cprintf("alloc_proc() correct!\n");  // 初始化正确
    }

    // 设置空闲进程的属性
    idleproc->pid = 0;  // PID 为 0
    idleproc->state = PROC_RUNNABLE;  // 状态为可运行
    idleproc->kstack = (uintptr_t)bootstack;  // 内核栈使用引导栈
    idleproc->need_resched = 1;  // 需要被调度
    set_proc_name(idleproc, "idle");  // 设置进程名为 "idle"
    nr_process++;  // 进程数量加 1

    current = idleproc;  // 将当前进程设置为空闲进程

    // 创建初始进程
    int pid = kernel_thread(init_main, "Hello world!!", 0);  // 创建内核线程
    if (pid <= 0)
    {
        panic("create init_main failed.\n");  // 创建失败则系统崩溃
    }

    initproc = find_proc(pid);  // 查找刚创建的进程
    set_proc_name(initproc, "init");  // 设置进程名为 "init"

    // 断言验证进程创建正确
    assert(idleproc != NULL && idleproc->pid == 0);  // 空闲进程 PID 为 0
    assert(initproc != NULL && initproc->pid == 1);  // 初始进程 PID 为 1
}

// cpu_idle - at the end of kern_init, the first kernel thread idleproc will do below works
// CPU 空闲 - 在 kern_init 结束时，第一个内核线程 idleproc 将执行下面的工作
void cpu_idle(void)
{
    while (1)  // 无限循环
    {
        if (current->need_resched)  // 如果当前进程需要被调度
        {
            schedule();  // 调用调度器
        }
    }
}
