# Lab5 分支任务：GDB调试系统调用流程
> 陈天祺 2313857

## 实验要求

通过双重GDB调试方案，观察操作系统中系统调用的完整流程，包括：
1. 用户态通过`ecall`指令进入内核态的过程
2. 内核态通过`sret`指令返回用户态的过程
3. QEMU模拟器如何通过软件模拟硬件处理特权指令
4. 理解TCG（Tiny Code Generator）翻译机制

## 实验过程

### 1. 三终端调试架构

#### 1.1 终端一：启动 QEMU 调试模式
```shell
make debug
# 随后等待 GDB 连接
```

#### 1.2 终端二：启动 GDB
```shell
make gdb
```

#### 1.3 终端三：附加 GDB 到 QEMU 进程
```shell
# 修改 ptrace 权限
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope

gdb ~/qemu-src/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64 \
    -p $(pgrep -f "qemu-system-riscv64")
```
- `pgrep "qemu-system-riscv64"`: 根据条件查找进程 PID
- `-f`: 在完整命令行中查找

> GDB附加到进程时，必须要显式指定符号文件来明确调试信息来源：`gdb <符号文件路径> -p <PID>`

### 2. 观察 ecall 系统调用（用户态 $\to$ 内核态）

#### 2.1 加载用户程序符号表

Lab5 实验框架下用户程序通过 `Link-in-Kernel` 方式链接进内核，符号表需要手动加载。此处以用户态程序 `__user_exit.out` 为例。

```c
(gdb) add-symbol-file obj/__user_exit.out
```

#### 2.2 添加断点与程序中断

```c
(gdb) b user/libs/syscall.c:19
(gdb) c
```

`ucore` 将中止于 `syscall` 函数，观察后续指令
>Lab5 中的 `syscall` 是用户态函数，封装了发起系统调用的逻辑，而一般意义上的 `syscall` 指内核态用于分发、执行系统调用的函数，发生于 `ecall` 陷入内核之后

```c
(gdb) c
Continuing.

Breakpoint 1, syscall (num=2) at user/libs/syscall.c:19
19          asm volatile (
(gdb) x/10i $pc
=> 0x8000f0 <syscall+28>:       sd      a6,128(sp)
   0x8000f2 <syscall+30>:       sd      a7,136(sp)
   0x8000f4 <syscall+32>:       sd      t1,32(sp)
   0x8000f6 <syscall+34>:       ld      a0,8(sp)
   0x8000f8 <syscall+36>:       ld      a1,40(sp)
   0x8000fa <syscall+38>:       ld      a2,48(sp)
   0x8000fc <syscall+40>:       ld      a3,56(sp)
   0x8000fe <syscall+42>:       ld      a4,64(sp)
   0x800100 <syscall+44>:       ld      a5,72(sp)
   0x800102 <syscall+46>:       ecall
```


#### 2.3 继续执行到 ecall 前 

不断执行 `si` 直到 `ecall` 指令的前一个指令，观察进程状态：

```c
(gdb) x/10i $pc 
=> 0x800102 <syscall+46>:       ecall
   0x800106 <syscall+50>:       sd      a0,28(sp)
   0x80010a <syscall+54>:       lw      a0,28(sp)
   0x80010c <syscall+56>:       addi    sp,sp,144
   0x80010e <syscall+58>:       ret
   0x800110 <sys_exit>: mv      a1,a0
   0x800112 <sys_exit+2>:       li      a0,1
   0x800114 <sys_exit+4>:       j       0x8000d4 <syscall>       
   0x800116 <sys_fork>: li      a0,2
   0x800118 <sys_fork+2>:       j       0x8000d4 <syscall>   
```

#### 2.4 观察寄存器状态

```c
(gdb) info registers pc sp a0 a1 a2 a3 a4 a5 a7
pc             0x800102 0x800102 <syscall+46>
sp             0x7fffff40       0x7fffff40
a0             0x2      2
a1             0x0      0
a2             0x0      0
a3             0x0      0
a4             0x0      0
a5             0x0      0
a7             0x0      0
```

- `pc = 0x800102 <syscall+46>`: 程序即将执行的下一条指令(`ecall`) 地址
- `sp = 0x7fffff40`: 栈顶指针
- `a0 = 2`: 保存参数或返回值，这里保存系统调用号 2(`fork`)

#### 2.5 对 QEMU 设置 riscv_cpu_do_interrupt 断点

```
(gdb) b riscv_cpu_do_interrupt
(gdb) c
```

- `riscv_cpu_do_interrupt`:  QEMU中处理所有中断和异常的核心函数

### 3. 内核态系统调用

#### 3.1 捕获 ecall 执行

终端 2 使用 `si` 执行 `ecall` 指令，触发 QEMU 的 `riscv_cpu_do_interrupt` 模拟硬件处理中断异常，终端 3 中 QEMU 暂停，捕获 `ecall` 执行。

```c
(gdb) c
Continuing.
[Switching to Thread 0x7f909cb7d640 (LWP 857)]

Thread 3 "qemu-system-ris" hit Breakpoint 1, riscv_cpu_do_interrupt (cs=0x5e0f0eaa3d20) at /home/myname/qemu-src/qemu-4.1.1/target/riscv/cpu_helper.c:507
507         RISCVCPU *cpu = RISCV_CPU(cs);
```

#### 3.2 观察 QEMU 异常处理

观察 QEMU 异常处理过程中的相关 CSR 寄存器：

```c
(gdb) info registers sstatus sepc scause stvec
sstatus        0x8000000000046020       -9223372036854489056     
sepc           0x800102 8388866
scause         0x8      8
stvec          0xffffffffc0200fa4       -1071640668
(gdb) info registers pc
pc             0xffffffffc0200fa8       0xffffffffc0200fa8 <__alltraps+4>
```

- 识别异常类型：`env->scause = 8`
- 保存返回地址：`env->sepc = 0x800102`
- 指向异常处理入口 `__alltraps`：`env->pc = 0xffffffffc0200fa4`

### 4. 观察 sret 返回（内核态 $\to$ 用户态）

#### 4.1 设置 __trapset 断点

```c
(gdb) b __trapret
(gdb) c
```

程序中断在异常处理返回前的最后阶段。

#### 4.2 继续执行到 sret 前

```c
(gdb) si
__trapret () at kern/trap/trapentry.S:133
133         sret
1: x/7i $pc
=> 0xffffffffc020106a <__trapret+86>:   sret
   0xffffffffc020106e <forkrets>:       mv      sp,a0
   0xffffffffc0201070 <forkrets+2>:
    j   0xffffffffc0201014 <__trapret>
   0xffffffffc0201072 <kernel_execve_ret>:
    addi        a1,a1,-288
   0xffffffffc0201076 <kernel_execve_ret+4>:
    ld  s1,280(a0)
   0xffffffffc020107a <kernel_execve_ret+8>:
    sd  s1,280(a1)
   0xffffffffc020107e <kernel_execve_ret+12>:
    ld  s1,272(a0)
```

观察相关 CSR 寄存器

```c
(gdb) info registers pc sepc sstatus
pc             0xffffffffc020106a       0xffffffffc020106a <__trapret+86>
sepc           0x800106 8388870
sstatus        0x8000000000046020       -9223372036854489056 
```

#### 4.3 对 QEMU 设置 helper_sret 断点

```
(gdb) b helper_sret
```

### 5. 返回用户态

终端 2 使用 `si` 执行 `sret` 指令，触发 QEMU  `helper_sret` 模拟异常返回，终端 3 中QEMU 暂停，捕获 `sret` 执行。

`sret` 执行后观察 `pc`:

```c
(gdb) info registers pc
pc             0x800106 0x800106 <syscall+50>
```

成功返回用户态。

---
## 附录

### GDB 基本命令

```shell
# 断点管理
b <位置>          # 设置断点
info breakpoints # 查看所有断点
delete <编号>     # 删除断点
clear            # 清除所有断点

# 执行控制
c                # 继续执行
si               # 单步执行一条汇编指令
ni               # 单步执行（不进入函数）
finish           # 执行到当前函数返回

# 查看信息
bt               # 查看调用栈
i r              # 查看所有寄存器
i r <寄存器名>    # 查看特定寄存器
x/i $pc          # 查看PC指向的指令
x/10i $pc        # 查看PC之后的10条指令
display/7i $pc   # 每次停止时自动显示7条指令

# 内存查看
x/10x $sp        # 查看栈内容（16进制）
x/s <地址>       # 查看字符串
p <变量>         # 打印变量值

# 源码查看
list             # 查看源码
disassemble      # 反汇编当前函数
```
