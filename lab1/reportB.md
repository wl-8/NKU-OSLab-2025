# lab1: 比麻雀更小的麻雀（最小可执行内核）

## 练习2: 使用GDB验证启动流程（2311208-魏来）

### 2.1 调试过程

#### 2.1.1 第一阶段：OpenSBI固件初始化（0x1000）

**GDB启动和连接**

打开终端，运行以下命令启动QEMU：
```bash
make debug
```
终端输出以下信息，表示QEMU已启动并等待GDB连接：
```bash
+ cc kern/init/entry.S
+ cc kern/init/init.c
+ cc kern/libs/stdio.c
+ cc kern/driver/console.c
+ cc libs/printfmt.c
+ cc libs/readline.c
+ cc libs/sbi.c
+ cc libs/string.c
+ ld bin/kernel
riscv64-unknown-elf-objcopy bin/kernel --strip-all -O binary bin/ucore.img
```
打开另一个终端，连接到QEMU的GDB服务器：
```bash
make gdb
```
终端输出以下信息，表示GDB已连接到QEMU：
```bash
riscv64-unknown-elf-gdb \
    -ex 'file bin/kernel' \
    -ex 'set arch riscv:rv64' \
    -ex 'target remote localhost:1234'
GNU gdb (SiFive GDB-Metal 10.1.0-2020.12.7) 10.1
Copyright (C) 2020 Free Software Foundation, Inc.
License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
Type "show copying" and "show warranty" for details.
This GDB was configured as "--host=x86_64-linux-gnu --target=riscv64-unknown-elf".
Type "show configuration" for configuration details.
For bug reporting instructions, please see:
<https://github.com/sifive/freedom-tools/issues>.
Find the GDB manual and other documentation resources online at:
    <http://www.gnu.org/software/gdb/documentation/>.

For help, type "help".
Type "apropos word" to search for commands related to "word".
bin/kernel: No such file or directory.
The target architecture is set to "riscv:rv64".
Remote debugging using localhost:1234
warning: No executable has been specified and target does not support
determining executable automatically.  Try using the "file" command.
0x0000000000001000 in ?? ()
(gdb) 
```
其中`0x0000000000001000 in ?? ()`表示当前程序计数器（PC）指向地址`0x1000`，这是RISC-V CPU的复位向量地址。

**查看寄存器状态**

通过以下命令查看寄存器状态：
```bash
(gdb) info register
```
确认程序计数器指向 `0x1000`，即OpenSBI固件的入口点；其它寄存器均为0，表示尚未执行任何指令。
```bash
ra             0x0      0x0
sp             0x0      0x0
gp             0x0      0x0
tp             0x0      0x0
t0             0x0      0
t1             0x0      0
t2             0x0      0
fp             0x0      0x0
s1             0x0      0
a0             0x0      0
a1             0x0      0
a2             0x0      0
a3             0x0      0
a4             0x0      0
a5             0x0      0
a6             0x0      0
a7             0x0      0
s2             0x0      0
s3             0x0      0
s4             0x0      0
s5             0x0      0
s6             0x0      0
s7             0x0      0
s8             0x0      0
s9             0x0      0
s10            0x0      0
s11            0x0      0
t3             0x0      0
t4             0x0      0
t5             0x0      0
t6             0x0      0
pc             0x1000   0x1000
dscratch       Could not fetch register "dscratch"; remote failure reply 'E14'
mucounteren    Could not fetch register "mucounteren"; remote failure reply 'E14'
```

**查看RISC-V硬件加电后最初执行的几条指令**

通过以下命令查看地址 `0x1000` 处的10条指令：
```bash
(gdb) x/10i 0x1000
```
输出结果显示CPU停在地址 `0x1000`：
```bash
=> 0x1000:      auipc   t0,0x0
   0x1004:      addi    a1,t0,32
   0x1008:      csrr    a0,mhartid
   0x100c:      ld      t0,24(t0)
   0x1010:      jr      t0
   0x1014:      unimp
   0x1016:      unimp
   0x1018:      unimp
   0x101a:      0x8000
   0x101c:      unimp
```
这几条指令主要完成以下功能：
```bash
0x1000:      auipc   t0,0x0        ; 加载当前PC相对地址到t0
0x1004:      addi    a1,t0,32      ; t0+32 -> a1
0x1008:      csrr    a0,mhartid    ; 获取硬件线程ID
0x100c:      ld      t0,24(t0)     ; 从t0+24处加载地址到t0
0x1010:      jr      t0            ; 跳转到t0中的地址
```
在0x1010处设置断点，然后检查t0的值：
```bash
(gdb) break *0x1010
(gdb) continue
(gdb) info registers t0
```
输出结果：
```bash
t0             0x80000000       2147483648
```
说明CPU将跳转到地址 `0x80000000` 继续执行。

#### 2.1.2 第二阶段：SBI固件主初始化和内核加载（0x80000000）

从`0x80000000`开始，OpenSBI固件执行主初始化代码，其核心目的包括：
- 从引导状态切换到内核运行状态
- 建立完整的运行时环境(堆栈、中断、内存管理)
- 最终将控制权交给真正的操作系统内核（加载到`0x80200000`）

**设置内核入口断点**
```bash
(gdb) b *0x80200000
```
输出结果：
```bash
Breakpoint 1 at 0x80200000: file kern/init/entry.S, line 7.
```

**继续执行直到断点触发（内核加载完成）**
```bash
(gdb) continue
```
输出结果：
```bash
Continuing.

Breakpoint 1, kern_entry () at kern/init/entry.S:7
7           la sp, bootstacktop
```
```bash
(gdb) info registers pc
```
输出结果：
```bash
pc             0x80200000       0x80200000 <kern_entry>
```
说明程序计数器已成功跳转到内核入口点 `0x80200000`。

**验证内核加载内容**
```bash
(gdb) x/1x 0x80200000
```
输出结果：
```bash
0x80200000 <kern_entry>:        0x00003117
```
确认内核代码已经成功加载到 `0x80200000` 地址。

#### 2.1.3 第三阶段：控制权移交内核（0x80200000）

**查看内核入口处的指令**
```bash
(gdb) x/50i 0x80200000
```
可以看到内核入口处的汇编指令和kern/init/entry.S以及kern/init/init.c中的代码功能完全对应：

汇编指令：
```bash
0x80200000 <kern_entry>:     auipc   sp,0x3
0x80200004 <kern_entry+4>:   mv      sp,sp
```
entry.S代码：
```assembly
kern_entry:
    la sp, bootstacktop
```
汇编指令：
```bash
0x80200008 <kern_entry+8>:   j       0x8020000a <kern_init>
```
entry.S代码：
```assembly
    tail kern_init
```
汇编指令：
```bash
0x8020000a <kern_init>:      auipc   a0,0x3
0x8020000e <kern_init+4>:    addi    a0,a0,-2
0x80200012 <kern_init+8>:    auipc   a2,0x3
0x80200016 <kern_init+12>:   addi    a2,a2,-10
0x8020001a <kern_init+16>:   addi    sp,sp,-16
0x8020001c <kern_init+18>:   li      a1,0
0x8020001e <kern_init+20>:   sub     a2,a2,a0
0x80200020 <kern_init+22>:   sd      ra,8(sp)
0x80200022 <kern_init+24>:   jal     ra,0x802004b6 <memset>
```
init.c代码：
```c
int kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);
```
汇编指令：
```bash
0x80200026 <kern_init+28>:   auipc   a1,0x0
0x8020002a <kern_init+32>:   addi    a1,a1,1186
0x8020002e <kern_init+36>:   auipc   a0,0x0
0x80200032 <kern_init+40>:   addi    a0,a0,1210
0x80200036 <kern_init+44>:   jal     ra,0x80200056 <cprintf>
```
init.c代码：
```c
    const char *message = "(THU.CST) os is loading ...\n";
    cprintf("%s\n\n", message);
```
汇编指令：
```bash
0x8020003a <kern_init+48>:   j       0x8020003a <kern_init+48>
```
init.c代码：
```c
    while (1)
        ;
```

### 2.2 问题答案

#### 2.2.1 RISC-V硬件加电后最初执行的几条指令位于什么地址？

RISC-V 硬件加电后，CPU 从地址 `0x1000` 开始执行指令。这是 RISC-V 架构的复位向量地址，对应于 OpenSBI 固件的入口点。

#### 2.2.2 它们主要完成了哪些功能？

这些指令主要完成了以下功能：

1. **获取硬件信息**：
   - `csrr a0,mhartid` - 获取当前硬件线程ID

2. **加载启动函数指针**：
   - `ld t0,24(t0)` - 从内存地址0x1018处加载函数指针
   - 准备跳转到下一阶段

3. **跳转到BIOS入口**：
   - `jr t0` - 跳转到加载的函数地址
   - 将控制权交给BIOS启动代码
