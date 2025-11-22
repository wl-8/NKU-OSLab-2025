## 练习1：加载应用程序并执行

### 设计实现过程

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

### 用户态进程从被调度到执行第一条指令的完整过程

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