#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

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
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL)
    {
        // LAB4:EXERCISE1 YOUR CODE
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

        // LAB5 YOUR CODE : (update LAB4 steps)
        /*
         * below fields(add in LAB5) in proc_struct need to be initialized
         * 下面是 LAB5 中 proc_struct 新增的字段，需要初始化：
         *       uint32_t wait_state;                        // waiting state - 等待状态
         *       struct proc_struct *cptr, *yptr, *optr;     // relations between processes - 进程间关系
         */
        proc->wait_state = 0;           // 等待状态初始化为0（不在等待）
        proc->cptr = NULL;              // child pointer - 子进程指针初始化为NULL
        proc->yptr = NULL;              // younger sibling pointer - younger兄弟进程指针初始化为NULL
        proc->optr = NULL;              // older sibling pointer - older兄弟进程指针初始化为NULL
    }
    return proc;
}

// set_proc_name - set the name of proc
// 设置进程名称 - 为进程设置名称
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
// 获取进程名称 - 从进程控制块获取进程名称
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc)
{
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL)
    {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc)
{
    list_del(&(proc->list_link));
    if (proc->optr != NULL)
    {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL)
    {
        proc->yptr->optr = proc->optr;
    }
    else
    {
        proc->parent->cptr = proc->optr;
    }
    nr_process--;
}

// get_pid - alloc a unique pid for process
// 获取 PID - 为进程分配一个唯一的 PID
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
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

// forkret -- 新线程/进程的第一个内核入口点
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
// 注意：forkret 的地址在 copy_thread 函数中设置
//      在 switch_to 之后，当前进程将在这里执行。
static void
forkret(void)
{
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
// 哈希进程 - 将进程添加到进程哈希表中
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc)
{
    list_del(&(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
// 查找进程 - 根据 PID 从进程哈希表中查找进程
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}

// kernel_thread - create a kernel thread using "fn" function
// NOTE: the contents of temp trapframe tf will be copied to
//       proc->tf in do_fork-->copy_thread function
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    tf.epc = (uintptr_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm)
{
    struct Page *page;
    if ((page = alloc_page()) == NULL)
    {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir_va, PGSIZE);

    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm)
{
    free_page(kva2page(mm->pgdir));
}

// copy_mm - process "proc" duplicate OR share process "current"'s mm according clone_flags
//         - if clone_flags & CLONE_VM, then "share" ; else "duplicate"
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    struct mm_struct *mm, *oldmm = current->mm;

    /* current is a kernel thread */
    if (oldmm == NULL)
    {
        return 0;
    }
    if (clone_flags & CLONE_VM)
    {
        mm = oldmm;
        goto good_mm;
    }
    int ret = -E_NO_MEM;
    if ((mm = mm_create()) == NULL)
    {
        goto bad_mm;
    }
    if (setup_pgdir(mm) != 0)
    {
        goto bad_pgdir_cleanup_mm;
    }
    lock_mm(oldmm);
    {
        ret = dup_mmap(mm, oldmm);
    }
    unlock_mm(oldmm);

    if (ret != 0)
    {
        goto bad_dup_cleanup_mmap;
    }

good_mm:
    mm_count_inc(mm);
    proc->mm = mm;
    proc->pgdir = PADDR(mm->pgdir);
    return 0;
bad_dup_cleanup_mmap:
    exit_mmap(mm);
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    return ret;
}

// copy_thread - setup the trapframe on the  process's kernel stack top and
//             - setup the kernel entry point and stack of process
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
    *(proc->tf) = *tf;

    // Set a0 to 0 so a child process knows it's just forked
    proc->tf->gpr.a0 = 0;
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;

    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)(proc->tf);
}

/* do_fork -     parent process for a new child process
 * @clone_flags: used to guide how to clone the child process
 * @stack:       the parent's user stack pointer. if stack==0, It means to fork a kernel thread.
 * @tf:          the trapframe info, which will be copied to child process's proc->tf
 */
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS)
    {
        goto fork_out;
    }
    ret = -E_NO_MEM;
    // LAB4:EXERCISE2 YOUR CODE
    /*
     * Some Useful MACROs, Functions and DEFINEs, you can use them in below implementation.
     * MACROs or Functions:
     *   alloc_proc:   create a proc struct and init fields (lab4:exercise1)
     *   setup_kstack: alloc pages with size KSTACKPAGE as process kernel stack
     *   copy_mm:      process "proc" duplicate OR share process "current"'s mm according clone_flags
     *                 if clone_flags & CLONE_VM, then "share" ; else "duplicate"
     *   copy_thread:  setup the trapframe on the  process's kernel stack top and
     *                 setup the kernel entry point and stack of process
     *   hash_proc:    add proc into proc hash_list
     *   get_pid:      alloc a unique pid for process
     *   wakeup_proc:  set proc->state = PROC_RUNNABLE
     * VARIABLES:
     *   proc_list:    the process set's list
     *   nr_process:   the number of process set
     */

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

    // LAB5 update: 设置子进程的父进程为当前进程，确保当前进程不在等待状态
    proc->parent = current;         // 设置父进程指针
    assert(current->wait_state == 0); // 确保父进程当前不在等待状态
    
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
        // LAB5 update: 使用 set_links 设置进程间的关系链接
        // set_links 会将进程加入 proc_list，并设置父子/兄弟关系
        set_links(proc);
    }
    local_intr_restore(intr_flag);
    //    6. call wakeup_proc to make the new child process RUNNABLE
    wakeup_proc(proc);
    //    7. set ret vaule using child proc's pid
    ret = proc->pid;

    // LAB5 YOUR CODE : (update LAB4 steps)
    // TIPS: you should modify your written code in lab4(step1 and step5), not add more code.
    /* Some Functions
     *    set_links:  set the relation links of process.  ALSO SEE: remove_links:  lean the relation links of process
     *    -------------------
     *    update step 1: set child proc's parent to current process, make sure current process's wait_state is 0
     *    update step 5: insert proc_struct into hash_list && proc_list, set the relation links of process
     */

fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}

// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process
int do_exit(int error_code)
{
    if (current == idleproc)
    {
        panic("idleproc exit.\n");
    }
    if (current == initproc)
    {
        panic("initproc exit.\n");
    }
    struct mm_struct *mm = current->mm;
    if (mm != NULL)
    {
        lsatp(boot_pgdir_pa);
        if (mm_count_dec(mm) == 0)
        {
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        current->mm = NULL;
    }
    current->state = PROC_ZOMBIE;
    current->exit_code = error_code;
    bool intr_flag;
    struct proc_struct *proc;
    local_intr_save(intr_flag);
    {
        proc = current->parent;
        if (proc->wait_state == WT_CHILD)
        {
            wakeup_proc(proc);
        }
        while (current->cptr != NULL)
        {
            proc = current->cptr;
            current->cptr = proc->optr;

            proc->yptr = NULL;
            if ((proc->optr = initproc->cptr) != NULL)
            {
                initproc->cptr->yptr = proc;
            }
            proc->parent = initproc;
            initproc->cptr = proc;
            if (proc->state == PROC_ZOMBIE)
            {
                if (initproc->wait_state == WT_CHILD)
                {
                    wakeup_proc(initproc);
                }
            }
        }
    }
    local_intr_restore(intr_flag);
    schedule();
    panic("do_exit will not return!! %d.\n", current->pid);
}

/* load_icode - 加载 ELF 格式的二进制程序作为当前进程的新内容
 * load_icode - load the content of binary program(ELF format) as the new content of current process
 * @binary:  二进制程序内容的内存地址 / the memory addr of the content of binary program
 * @size:  二进制程序内容的大小 / the size of the content of binary program
 * 
 * 功能说明：
 * 1. 为当前进程创建新的内存管理结构（mm_struct）
 * 2. 创建新的页目录表（PDT）
 * 3. 解析 ELF 文件，将程序的各个段（代码段、数据段、BSS段）加载到内存
 * 4. 建立用户栈
 * 5. 设置进程的页表和内存管理信息
 * 6. 设置 trapframe，准备返回用户态执行
 */
static int
load_icode(unsigned char *binary, size_t size)
{
    // 检查当前进程的内存管理结构是否为空
    // 如果不为空，说明进程已经有内存空间，不能重复加载
    if (current->mm != NULL)
    {
        panic("load_icode: current->mm must be empty.\n");
    }

    int ret = -E_NO_MEM;  // 默认返回内存不足错误
    struct mm_struct *mm;
    
    //(1) 为当前进程创建新的内存管理结构
    // create a new mm for current process
    if ((mm = mm_create()) == NULL)
    {
        goto bad_mm;  // 创建失败，跳转到错误处理
    }
    
    //(2) 创建新的页目录表（PDT），mm->pgdir 保存页目录的内核虚拟地址
    // create a new PDT, and mm->pgdir = kernel virtual addr of PDT
    if (setup_pgdir(mm) != 0)
    {
        goto bad_pgdir_cleanup_mm;  // 创建失败，需要清理 mm
    }
    
    //(3) 复制 TEXT/DATA 段，构建 BSS 段到进程的内存空间
    // copy TEXT/DATA section, build BSS parts in binary to memory space of process
    struct Page *page = NULL;  // 用于保存当前处理的物理页面，初始化为 NULL 避免未初始化使用
    
    //(3.1) 获取二进制程序的文件头（ELF 格式）
    // get the file header of the binary program (ELF format)
    struct elfhdr *elf = (struct elfhdr *)binary;
    
    //(3.2) 获取二进制程序的程序段头表（Program Header Table）
    // get the entry of the program section headers of the binary program (ELF format)
    struct proghdr *ph = (struct proghdr *)(binary + elf->e_phoff);
    
    //(3.3) 检查程序是否有效（验证 ELF 魔数）
    // This program is valid?
    if (elf->e_magic != ELF_MAGIC)
    {
        ret = -E_INVAL_ELF;  // 无效的 ELF 文件
        goto bad_elf_cleanup_pgdir;
    }

    uint32_t vm_flags, perm;  // vm_flags: 虚拟内存区域标志，perm: 页表项权限位
    struct proghdr *ph_end = ph + elf->e_phnum;  // 计算程序段头表的结束位置
    
    // 遍历所有程序段头
    for (; ph < ph_end; ph++)
    {
        //(3.4) 查找每个需要加载的程序段
        // find every program section headers
        if (ph->p_type != ELF_PT_LOAD)  // 只处理 LOAD 类型的段
        {
            continue;  // 跳过非 LOAD 段（如动态链接信息等）
        }
        
        // 检查段的有效性：文件中的大小不应大于内存中的大小
        if (ph->p_filesz > ph->p_memsz)
        {
            ret = -E_INVAL_ELF;
            goto bad_cleanup_mmap;
        }
        
        // p_filesz == 0 表示纯 BSS 段（未初始化数据段）
        if (ph->p_filesz == 0)
        {
            // continue ; // 注释掉的代码，实际上继续处理
        }
        
        //(3.5) 调用 mm_map 函数设置新的虚拟内存区域（VMA）
        // call mm_map fun to setup the new vma (ph->p_va, ph->p_memsz)
        // 根据 ELF 段标志设置虚拟内存区域和页表权限
        vm_flags = 0, perm = PTE_U | PTE_V;  // 基本权限：用户可访问 + 有效
        
        // 根据 ELF 段标志设置虚拟内存区域标志
        if (ph->p_flags & ELF_PF_X)  // 可执行
            vm_flags |= VM_EXEC;
        if (ph->p_flags & ELF_PF_W)  // 可写
            vm_flags |= VM_WRITE;
        if (ph->p_flags & ELF_PF_R)  // 可读
            vm_flags |= VM_READ;
        
        // 为 RISC-V 修改页表权限位
        // modify the perm bits here for RISC-V
        if (vm_flags & VM_READ)
            perm |= PTE_R;  // 可读
        if (vm_flags & VM_WRITE)
            perm |= (PTE_W | PTE_R);  // 可写（RISC-V 要求可写必须可读）
        if (vm_flags & VM_EXEC)
            perm |= PTE_X;  // 可执行
        
        // 在内存管理结构中建立虚拟内存区域映射
        if ((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL)) != 0)
        {
            goto bad_cleanup_mmap;
        }
        
        // 准备复制数据
        unsigned char *from = binary + ph->p_offset;  // 源地址：二进制文件中的偏移
        size_t off, size;  // off: 页内偏移，size: 本次复制大小
        uintptr_t start = ph->p_va, end, la = ROUNDDOWN(start, PGSIZE);  // start: 虚拟地址起始，la: 页对齐的地址

        ret = -E_NO_MEM;  // 设置默认错误码

        //(3.6) 分配内存，并复制每个程序段的内容 (from, from+end) 到进程内存 (la, la+end)
        // alloc memory, and copy the contents of every program section (from, from+end) to process's memory (la, la+end)
        
        end = ph->p_va + ph->p_filesz;  // 文件内容的结束地址
        
        //(3.6.1) 复制二进制程序的 TEXT/DATA 段（有文件内容的部分）
        // copy TEXT/DATA section of binary program
        while (start < end)
        {
            // 为虚拟地址 la 分配一个物理页面
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL)
            {
                goto bad_cleanup_mmap;  // 分配失败，清理并返回
            }
            
            // 计算本次复制的偏移和大小
            off = start - la;         // 页内偏移
            size = PGSIZE - off;      // 本页剩余空间
            la += PGSIZE;             // 移动到下一页
            
            // 如果段结束位置在本页内，调整复制大小
            if (end < la)
            {
                size -= la - end;
            }
            
            // 从二进制文件复制数据到物理页面
            memcpy(page2kva(page) + off, from, size);
            start += size, from += size;  // 更新源和目标位置
        }

        //(3.6.2) 构建二进制程序的 BSS 段（未初始化数据段，需要清零）
        // build BSS section of binary program
        end = ph->p_va + ph->p_memsz;  // BSS 段的结束地址（内存中的大小）
        
        // 如果上一个页面还没填满，继续在该页面填充零
        if (start < la)
        {
            /* ph->p_memsz == ph->p_filesz */
            // 如果内存大小等于文件大小，说明没有 BSS 段
            if (start == end)
            {
                continue;  // 跳过此段，处理下一段
            }
            
            // 计算需要清零的部分
            off = start + PGSIZE - la;  // 页内偏移
            size = PGSIZE - off;         // 需要清零的大小
            if (end < la)
            {
                size -= la - end;
            }
            
            // 在上一个分配的页面中清零（BSS 段）
            memset(page2kva(page) + off, 0, size);
            start += size;
            
            // 断言检查：确保清零后的位置正确
            assert((end < la && start == end) || (end >= la && start == la));
        }
        
        // 如果还有更多 BSS 内容，继续分配新页面并清零
        while (start < end)
        {
            // 为虚拟地址 la 分配新的物理页面
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL)
            {
                goto bad_cleanup_mmap;
            }
            
            // 计算需要清零的大小
            off = start - la;
            size = PGSIZE - off;
            la += PGSIZE;
            if (end < la)
            {
                size -= la - end;
            }
            
            // 将新分配的页面清零（BSS 段）
            memset(page2kva(page) + off, 0, size);
            start += size;
        }
    }
    
    //(4) 构建用户栈内存
    // build user stack memory
    vm_flags = VM_READ | VM_WRITE | VM_STACK;  // 用户栈权限：可读、可写、栈标志
    
    // 在用户地址空间顶部预留栈空间（从 USTACKTOP-USTACKSIZE 到 USTACKTOP）
    if ((ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE, vm_flags, NULL)) != 0)
    {
        goto bad_cleanup_mmap;
    }
    
    // 预先分配 4 个物理页面给用户栈（从栈顶向下）
    // 这样可以避免用户程序刚开始运行就触发缺页异常
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - PGSIZE, PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - 2 * PGSIZE, PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - 3 * PGSIZE, PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - 4 * PGSIZE, PTE_USER) != NULL);

    //(5) 设置当前进程的 mm、页目录，并设置 satp 寄存器为页目录的物理地址
    // set current process's mm, cr3, and set satp reg = physical addr of Page Directory
    mm_count_inc(mm);                    // 增加内存管理结构的引用计数
    current->mm = mm;                    // 设置当前进程的内存管理结构
    current->pgdir = PADDR(mm->pgdir);   // 设置页目录的物理地址
    lsatp(PADDR(mm->pgdir));             // 加载页表到 satp 寄存器，切换到新的地址空间

    //(6) 为用户环境设置 trapframe（陷阱帧）
    // setup trapframe for user environment
    struct trapframe *tf = current->tf;
    
    // 保存当前的 sstatus 寄存器值
    // Keep sstatus
    uintptr_t sstatus = tf->status;
    
    // 清空 trapframe（准备设置新的用户态上下文）
    memset(tf, 0, sizeof(struct trapframe));
    
    /* LAB5:EXERCISE1 2311208
     * 需要设置 tf->gpr.sp, tf->epc, tf->status
     * should set tf->gpr.sp, tf->epc, tf->status
     * 
     * 注意：如果正确设置 trapframe，那么用户态进程可以从内核返回到用户态。因此：
     * NOTICE: If we set trapframe correctly, then the user level process can return to USER MODE from kernel. So
     * 
     *   tf->gpr.sp 应该是用户栈顶（sp 的值）
     *   tf->gpr.sp should be user stack top (the value of sp)
     * 
     *   tf->epc 应该是用户程序的入口点（sepc 的值）
     *   tf->epc should be entry point of user program (the value of sepc)
     * 
     *   tf->status 应该适合用户程序（sstatus 的值）
     *   tf->status should be appropriate for user program (the value of sstatus)
     * 
     *   提示：检查 SSTATUS 中 SPP、SPIE 的含义，使用 SSTATUS_SPP、SSTATUS_SPIE（在 riscv.h 中定义）
     *   hint: check meaning of SPP, SPIE in SSTATUS, use them by SSTATUS_SPP, SSTATUS_SPIE(defined in risv.h)
     */
    
    // 设置用户栈指针为用户栈顶
    tf->gpr.sp = USTACKTOP;
    
    // 设置程序入口地址（用户程序开始执行的位置）
    tf->epc = elf->e_entry;
    
    // 设置状态寄存器：
    // - 清除 SPP 位（Supervisor Previous Privilege）：返回用户态（U-mode）
    // - 设置 SPIE 位（Supervisor Previous Interrupt Enable）：返回用户态后启用中断
    tf->status = (sstatus & ~SSTATUS_SPP) | SSTATUS_SPIE;
    
    ret = 0;  // 成功
out:
    return ret;
    
// 错误处理：采用级联清理，确保已分配的资源被正确释放
bad_cleanup_mmap:
    exit_mmap(mm);                // 释放所有映射的虚拟内存区域
bad_elf_cleanup_pgdir:
    put_pgdir(mm);                // 释放页目录表
bad_pgdir_cleanup_mm:
    mm_destroy(mm);               // 销毁内存管理结构
bad_mm:
    goto out;                     // 返回错误码
}

// do_execve - call exit_mmap(mm)&put_pgdir(mm) to reclaim memory space of current process
//           - call load_icode to setup new memory space accroding binary prog.
int do_execve(const char *name, size_t len, unsigned char *binary, size_t size)
{
    struct mm_struct *mm = current->mm;
    if (!user_mem_check(mm, (uintptr_t)name, len, 0))
    {
        return -E_INVAL;
    }
    if (len > PROC_NAME_LEN)
    {
        len = PROC_NAME_LEN;
    }

    char local_name[PROC_NAME_LEN + 1];
    memset(local_name, 0, sizeof(local_name));
    memcpy(local_name, name, len);

    if (mm != NULL)
    {
        cputs("mm != NULL");
        lsatp(boot_pgdir_pa);
        if (mm_count_dec(mm) == 0)
        {
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        current->mm = NULL;
    }
    int ret;
    if ((ret = load_icode(binary, size)) != 0)
    {
        goto execve_exit;
    }
    set_proc_name(current, local_name);
    return 0;

execve_exit:
    do_exit(ret);
    panic("already exit: %e.\n", ret);
}

// do_yield - ask the scheduler to reschedule
int do_yield(void)
{
    current->need_resched = 1;
    return 0;
}

// do_wait - wait one OR any children with PROC_ZOMBIE state, and free memory space of kernel stack
//         - proc struct of this child.
// NOTE: only after do_wait function, all resources of the child proces are free.
int do_wait(int pid, int *code_store)
{
    struct mm_struct *mm = current->mm;
    if (code_store != NULL)
    {
        if (!user_mem_check(mm, (uintptr_t)code_store, sizeof(int), 1))
        {
            return -E_INVAL;
        }
    }

    struct proc_struct *proc;
    bool intr_flag, haskid;
repeat:
    haskid = 0;
    if (pid != 0)
    {
        proc = find_proc(pid);
        if (proc != NULL && proc->parent == current)
        {
            haskid = 1;
            if (proc->state == PROC_ZOMBIE)
            {
                goto found;
            }
        }
    }
    else
    {
        proc = current->cptr;
        for (; proc != NULL; proc = proc->optr)
        {
            haskid = 1;
            if (proc->state == PROC_ZOMBIE)
            {
                goto found;
            }
        }
    }
    if (haskid)
    {
        current->state = PROC_SLEEPING;
        current->wait_state = WT_CHILD;
        schedule();
        if (current->flags & PF_EXITING)
        {
            do_exit(-E_KILLED);
        }
        goto repeat;
    }
    return -E_BAD_PROC;

found:
    if (proc == idleproc || proc == initproc)
    {
        panic("wait idleproc or initproc.\n");
    }
    if (code_store != NULL)
    {
        *code_store = proc->exit_code;
    }
    local_intr_save(intr_flag);
    {
        unhash_proc(proc);
        remove_links(proc);
    }
    local_intr_restore(intr_flag);
    put_kstack(proc);
    kfree(proc);
    return 0;
}

// do_kill - kill process with pid by set this process's flags with PF_EXITING
int do_kill(int pid)
{
    struct proc_struct *proc;
    if ((proc = find_proc(pid)) != NULL)
    {
        if (!(proc->flags & PF_EXITING))
        {
            proc->flags |= PF_EXITING;
            if (proc->wait_state & WT_INTERRUPTED)
            {
                wakeup_proc(proc);
            }
            return 0;
        }
        return -E_KILLED;
    }
    return -E_INVAL;
}

// kernel_execve - do SYS_exec syscall to exec a user program called by user_main kernel_thread
static int
kernel_execve(const char *name, unsigned char *binary, size_t size)
{
    int64_t ret = 0, len = strlen(name);
    //   ret = do_execve(name, len, binary, size);
    asm volatile(
        "li a0, %1\n"
        "lw a1, %2\n"
        "lw a2, %3\n"
        "lw a3, %4\n"
        "lw a4, %5\n"
        "li a7, 10\n"
        "ebreak\n"
        "sw a0, %0\n"
        : "=m"(ret)
        : "i"(SYS_exec), "m"(name), "m"(len), "m"(binary), "m"(size)
        : "memory");
    cprintf("ret = %d\n", ret);
    return ret;
}

#define __KERNEL_EXECVE(name, binary, size) ({           \
    cprintf("kernel_execve: pid = %d, name = \"%s\".\n", \
            current->pid, name);                         \
    kernel_execve(name, binary, (size_t)(size));         \
})

#define KERNEL_EXECVE(x) ({                                    \
    extern unsigned char _binary_obj___user_##x##_out_start[], \
        _binary_obj___user_##x##_out_size[];                   \
    __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,    \
                    _binary_obj___user_##x##_out_size);        \
})

#define __KERNEL_EXECVE2(x, xstart, xsize) ({   \
    extern unsigned char xstart[], xsize[];     \
    __KERNEL_EXECVE(#x, xstart, (size_t)xsize); \
})

#define KERNEL_EXECVE2(x, xstart, xsize) __KERNEL_EXECVE2(x, xstart, xsize)

// user_main - kernel thread used to exec a user program
static int
user_main(void *arg)
{
#ifdef TEST
    KERNEL_EXECVE2(TEST, TESTSTART, TESTSIZE);
#else
    KERNEL_EXECVE(exit);
#endif
    panic("user_main execve failed.\n");
}

// init_main - the second kernel thread used to create user_main kernel threads
static int
init_main(void *arg)
{
    size_t nr_free_pages_store = nr_free_pages();
    size_t kernel_allocated_store = kallocated();

    int pid = kernel_thread(user_main, NULL, 0);
    if (pid <= 0)
    {
        panic("create user_main failed.\n");
    }

    while (do_wait(0, NULL) == 0)
    {
        schedule();
    }

    cprintf("all user-mode processes have quit.\n");
    assert(initproc->cptr == NULL && initproc->yptr == NULL && initproc->optr == NULL);
    assert(nr_process == 2);
    assert(list_next(&proc_list) == &(initproc->list_link));
    assert(list_prev(&proc_list) == &(initproc->list_link));

    cprintf("init check memory pass.\n");
    return 0;
}

// proc_init - set up the first kernel thread idleproc "idle" by itself and
//           - create the second kernel thread init_main
void proc_init(void)
{
    int i;

    list_init(&proc_list);
    for (i = 0; i < HASH_LIST_SIZE; i++)
    {
        list_init(hash_list + i);
    }

    if ((idleproc = alloc_proc()) == NULL)
    {
        panic("cannot alloc idleproc.\n");
    }

    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    set_proc_name(idleproc, "idle");
    nr_process++;

    current = idleproc;

    int pid = kernel_thread(init_main, NULL, 0);
    if (pid <= 0)
    {
        panic("create init_main failed.\n");
    }

    initproc = find_proc(pid);
    set_proc_name(initproc, "init");

    assert(idleproc != NULL && idleproc->pid == 0);
    assert(initproc != NULL && initproc->pid == 1);
}

// cpu_idle - at the end of kern_init, the first kernel thread idleproc will do below works
void cpu_idle(void)
{
    while (1)
    {
        if (current->need_resched)
        {
            schedule();
        }
    }
}
