# 2025 操作系统 Lab5
> 团队成员: 2313857陈天祺 & 2311208魏来 & 2312166王旭

## 实验目的
---

- 了解第一个用户进程创建过程
- 了解系统调用框架的实现机制 
- 了解ucore如何实现系统调用sys_fork/sys_exec/sys_exit/sys_wait来进行进程管理

## 实验内容
---

### 练习0：填写已有实验
本实验依赖实验2/3/4。请把你做的实验2/3/4的代码填入本实验中代码中有“LAB2”/“LAB3”/“LAB4”的注释相应部分。注意：为了能够正确执行lab5的测试应用程序，可能需对已完成的实验2/3/4的代码进行进一步改进。

### 练习1: 加载应用程序并执行（需要编码）
`do_execv` 函数调用 `load_icode`（位于 kern/process/proc.c）来加载并解析一个处于内存中的ELF执行文件格式的应用程序。你需要补充 `load_icode` 的第6步，建立相应的用户内存空间来放置应用程序的代码段、数据段等，且要设置好proc_struct结构中的成员变量trapframe中的内容，确保在执行此进程后，能够从应用程序设定的起始执行地址开始执行。需设置正确的trapframe内容。

请在实验报告中简要说明你的设计实现过程，并回答以下问题：
**请简要描述这个用户态进程被ucore选择占用CPU执行（RUNNING态）到具体执行应用程序第一条指令的整个经过。**

### 练习2: 父进程复制自己的内存空间给子进程（需要编码）
创建子进程的函数 `do_fork` 在执行中将拷贝当前进程（即父进程）的用户内存地址空间中的合法内容到新进程中（子进程），完成内存资源的复制。具体是通过 `copy_range` 函数（位于kern/mm/pmm.c）实现的，请补充 `copy_range` 的实现，确保能够正确执行。

请在实验报告中简要说明你的设计实现过程，并回答以下问题：
**如何设计实现Copy on Write机制？给出概要设计，鼓励给出详细设计。**

> Copy-on-write（简称COW）的基本概念是指如果有多个使用者对一个资源A（比如内存块）进行读操作，则每个使用者只需获得一个指向同一个资源A的指针，就可以该资源了。若某使用者需要对这个资源A进行写操作，系统会对该资源进行拷贝操作，从而使得该“写操作”使用者获得一个该资源A的“私有”拷贝—资源B，可对资源B进行写操作。该“写操作”使用者对资源B的改变对于其他的使用者而言是不可见的，因为其他使用者看到的还是资源A。

### 练习3: 阅读分析源代码，理解进程执行 fork/exec/wait/exit 的实现，以及系统调用的实现（不需要编码）
请在实验报告中简要说明你对 fork/exec/wait/exit函数的分析，并回答如下问题：

1. 请分析 fork/exec/wait/exit 的执行流程。重点关注哪些操作是在用户态完成，哪些是在内核态完成？内核态与用户态程序是如何交错执行的？内核态执行结果是如何返回给用户程序的？

2. 请给出ucore中一个用户态进程的执行状态生命周期图（包执行状态，执行状态之间的变换关系，以及产生变换的事件或函数调用）。（字符方式画即可）

执行：`make grade`。如果所显示的应用程序检测都输出ok，则基本正确。（使用的是qemu-1.0.1）

### 扩展练习 Challenge

#### Challenge1：实现 Copy on Write （COW）机制
给出实现源码,测试用例和设计报告（包括在 cow 情况下的各种状态转换（类似有限状态自动机）的说明）。

这个扩展练习涉及到本实验和上一个实验“虚拟内存管理”。在ucore操作系统中，当一个用户父进程创建自己的子进程时，父进程会把其申请的用户空间设置为只读，子进程可共享父进程占用的用户内存空间中的页面（这就是一个共享的资源）。当其中任何一个进程修改此用户内存空间中的某页面时，ucore会通过page fault异常获知该操作，并完成拷贝内存页面，使得两个进程都有各自的内存页面。这样一个进程所做的修改不会被另外一个进程可见了。请在ucore中实现这样的COW机制。

由于COW实现比较复杂，容易引入bug，请参考 https://dirtycow.ninja/ 看看能否在ucore的COW实现中模拟这个错误和解决方案。需要有解释。

#### Challenge2：回答问题

1. 说明该用户程序是何时被预先加载到内存中的？
2. 与我们常用操作系统的加载有何区别，原因是什么？

## 实验过程
---

### 练习1：加载应用程序并执行

#### 设计实现过程

需要在 `load_icode` 函数的第6步设置trapframe，使得用户进程能够从内核态正确切换到用户态执行。

**实现代码**（`kern/process/proc.c`）：

```c
tf->gpr.sp = USTACKTOP;
tf->epc = elf->e_entry;
tf->status = (sstatus & ~SSTATUS_SPP) | SSTATUS_SPIE;
```

**关键点说明**：

1. **`tf->gpr.sp = USTACKTOP;`**：设置用户栈指针为用户栈顶
2. **`tf->epc = elf->e_entry;`**：设置程序计数器为ELF文件的入口地址
3. **`tf->status = (sstatus & ~SSTATUS_SPP) | SSTATUS_SPIE;`**：
   - 清除SPP位：返回用户态（U-mode）
   - 设置SPIE位：允许中断在用户态被启用

#### 用户态进程从被调度到执行第一条指令的完整过程

```
阶段1：init_main 被调度执行
--------------------------------------
1. 调度器选择 initproc
   schedule() → proc_run(initproc) → switch_to()

2. initproc 开始执行 init_main():
   int pid = kernel_thread(user_main, NULL, 0);  ← 创建 user_main 内核线程 (PID 2)

3. initproc 等待子进程：
   while (do_wait(0, NULL) == 0) {
       schedule();  ← 让出CPU，等待子进程退出
   }

阶段2：user_main 内核线程被调度执行
--------------------------------------
1. 调度器选择 user_main (PID 2)
   schedule() → proc_run(user_main) → switch_to()

2. user_main 首次执行流程：
   - switch_to() 的 ret 指令跳转到 forkret()
   - forkret() → forkrets() → __trapret → sret
   - 跳转到 kernel_thread_entry() → 调用 user_main()

3. user_main 函数开始执行：
   user_main(void *arg) {
       KERNEL_EXECVE(exit);  ← 加载用户程序 "exit"
       panic("user_main execve failed.\n");
   }

阶段3：kernel_execve 触发 ebreak 异常
--------------------------------------
1. KERNEL_EXECVE 展开为 kernel_execve("exit", ...)

2. kernel_execve 执行：
   asm volatile(
       "li a0, SYS_exec\n"        // 设置系统调用号
       "lw a1, %2\n"              // a1 = name
       "lw a2, %3\n"              // a2 = len
       "lw a3, %4\n"              // a3 = binary
       "lw a4, %5\n"              // a4 = size
       "li a7, 10\n"              // a7 = 10 (特殊标记)
       "ebreak\n"                 // ← 触发断点异常！
   );

阶段4：ebreak 异常处理 - 关键转换！
--------------------------------------
1. 硬件自动响应 ebreak 异常：
   - PC ← __alltraps  (跳转到异常处理入口)
   - 进入内核态处理异常

2. 异常处理流程：
   case CAUSE_BREAKPOINT:
     if (tf->gpr.a7 == 10) {              // 检查是否是 kernel_execve
         tf->epc += 4;                    // 跳过 ebreak 指令
         syscall();                       // 调用系统调用处理
         kernel_execve_ret(tf, kstacktop); // 特殊返回机制
     }

阶段5：syscall 处理 SYS_exec
--------------------------------------
1. syscall() → sys_exec() → do_execve()

2. do_execve 执行进程转换：
   // 释放当前内核线程的内存空间 (user_main 的 mm 为 NULL)

   // 创建用户内存空间
   struct mm_struct *mm = mm_create();
   setup_pgdir(mm);
   current->mm = mm;

   // 加载 ELF 用户程序
   load_icode(binary, size):
     - 解析 ELF 文件
     - 建立代码段、数据段、BSS段
     - 分配用户栈空间

     // 设置 trapframe - 关键！
     current->tf->gpr.sp = USTACKTOP;           // 用户栈指针
     current->tf->epc = elf->e_entry;           // 用户程序入口
     current->tf->status = (sstatus & ~SSTATUS_SPP) | SSTATUS_SPIE; // 用户态标志

阶段6：kernel_execve_ret 特殊返回
--------------------------------------
此时 user_main 内核线程已经转换成用户进程！

1. kernel_execve_ret 复制 trapframe：
   - 将当前 trapframe 复制到新的位置
   - move sp, 新的 trapframe 地址
   - j __trapret  ← 直接跳转到标准返回路径

阶段7：__trapret 返回用户态
--------------------------------------
1. __trapret 恢复用户态上下文：
   __trapret:
       RESTORE_ALL        // 从 trapframe 恢复所有寄存器：
       - sp ← USTACKTOP  (用户栈)
       - 其他通用寄存器恢复
       - sepc ← elf->e_entry (用户程序入口)

       sret               // 返回用户态

2. sret 指令执行：
   - PC ← sepc           (跳转到用户程序第一条指令)
   - 特权级切换到 U-mode (用户态)
   - 中断使能恢复

阶段8：用户程序开始执行
--------------------------------------
现在 CPU 处于用户态：
- PC = 用户程序入口地址 (如 exit.c 的 main 函数)
- SP = USTACKTOP (用户栈顶)
- 特权级 = U-mode
- 开始执行用户程序的第一条指令！
```