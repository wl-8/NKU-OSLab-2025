#ifndef __KERN_PROCESS_PROC_H__  // 防止头文件重复包含的宏保护
#define __KERN_PROCESS_PROC_H__

// 包含必要的头文件
#include <defs.h>      // 基本定义
#include <list.h>      // 链表实现
#include <trap.h>      // 陷阱和中断处理
#include <memlayout.h> // 内存布局定义

// process's state in his life cycle
// 进程在生命周期中的状态
enum proc_state
{
    PROC_UNINIT = 0, // uninitialized - 未初始化状态
    PROC_SLEEPING,   // sleeping - 睡眠状态（等待某个条件）
    PROC_RUNNABLE,   // runnable(maybe running) - 可运行状态（可能正在运行）
    PROC_ZOMBIE,     // almost dead, and wait parent proc to reclaim his resource
                     // 僵尸状态，即将死亡，等待父进程回收资源
};

// 进程上下文结构
// 保存进程在切换时需要保存的寄存器状态
struct context
{
    uintptr_t ra;   // 返回地址寄存器（return address）
    uintptr_t sp;   // 栈指针寄存器（stack pointer）
    uintptr_t s0;   // 被调用者保存寄存器 s0-s11（callee-saved registers）
    uintptr_t s1;   // 这些寄存器在函数调用时需要由被调用函数保存和恢复
    uintptr_t s2;
    uintptr_t s3;
    uintptr_t s4;
    uintptr_t s5;
    uintptr_t s6;
    uintptr_t s7;
    uintptr_t s8;
    uintptr_t s9;
    uintptr_t s10;
    uintptr_t s11;
};

#define PROC_NAME_LEN 15        // 进程名最大长度（不包括结尾的 \0）
#define MAX_PROCESS 4096        // 系统支持的最大进程数量
#define MAX_PID (MAX_PROCESS * 2) // 最大进程 ID（设置为最大进程数的两倍以避免 PID 冲突）

extern list_entry_t proc_list;  // 全局进程链表，包含所有进程

// 进程控制块结构（Process Control Block, PCB）
// 记录进程的所有相关信息
struct proc_struct
{
    enum proc_state state;        // Process state - 进程当前状态
    int pid;                      // Process ID - 进程标识符
    int runs;                     // the running times of Proces - 进程被调度运行的次数
    uintptr_t kstack;             // Process kernel stack - 进程内核栈的地址
    volatile bool need_resched;   // bool value: need to be rescheduled to release CPU?
                                  // 是否需要重新调度以释放 CPU
    struct proc_struct *parent;   // the parent process - 父进程指针
    struct mm_struct *mm;         // Process's memory management field - 进程内存管理结构
    struct context context;       // Switch here to run process - 进程上下文，用于进程切换
    struct trapframe *tf;         // Trap frame for current interrupt - 当前中断的陷阱帧
    uintptr_t pgdir;              // the base addr of Page Directroy Table(PDT)
                                  // 页目录表的基地址
    uint32_t flags;               // Process flag - 进程标志
    char name[PROC_NAME_LEN + 1]; // Process name - 进程名称（包括结尾的 \0）
    list_entry_t list_link;       // Process link list - 进程在全局列表中的链表节点
    list_entry_t hash_link;       // Process hash list - 进程在哈希表中的链表节点
};

// 宏定义：从链表节点获取进程控制块
// le: 链表节点指针, member: 成员名（list_link 或 hash_link）
#define le2proc(le, member) \
    to_struct((le), struct proc_struct, member)

// 全局进程指针
extern struct proc_struct *idleproc,  // idle 进程（空闲进程）
                         *initproc,  // 初始化进程
                         *current;   // 当前运行的进程

// 进程管理相关函数声明

void proc_init(void);  // 初始化进程管理子系统
void proc_run(struct proc_struct *proc);  // 运行指定进程
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);  // 创建内核线程

// 进程名称管理函数
char *set_proc_name(struct proc_struct *proc, const char *name);  // 设置进程名称
char *get_proc_name(struct proc_struct *proc);  // 获取进程名称
void cpu_idle(void) __attribute__((noreturn));  // CPU 空闲循环（永不返回）

// 进程生命周期管理函数
struct proc_struct *find_proc(int pid);  // 根据 PID 查找进程
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);  // 创建子进程
int do_exit(int error_code);  // 终止当前进程

#endif /* !__KERN_PROCESS_PROC_H__ */
