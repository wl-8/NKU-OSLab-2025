# lab3：中断与中断处理流程

## 扩展练习Challenge3：完善异常中断

### 4.1 实现代码

该实验的核心实现在 `kern/trap/trap.c` 文件的 `exception_handler()` 函数中，具体代码如下：

```c
case CAUSE_ILLEGAL_INSTRUCTION:
    // 非法指令异常处理
    /* LAB3 CHALLENGE3   2311208 :  */
    /*(1)输出指令异常类型（ Illegal instruction）
     *(2)输出异常指令地址
     *(3)更新 tf->epc寄存器
    */
    cprintf("Exception type: Illegal instruction\n");
    cprintf("Illegal instruction caught at 0x%08x\n", tf->epc);

    // 检查指令长度，如果是压缩指令（16位）则+2，否则+4
    {
        unsigned short *instr = (unsigned short*)tf->epc;
        unsigned short instruction = *instr;

        if ((instruction & 0x3) != 0x3) {
            // 压缩指令 (16位)
            cprintf("Instruction word: 0x%04x\n", instruction);
            tf->epc += 2;
            cprintf("Instruction length: 2 bytes (compressed instruction)\n");
        } else {
            // 标准指令 (32位)
            unsigned int full_instruction = *(unsigned int*)tf->epc;
            cprintf("Instruction word: 0x%08x\n", full_instruction);
            tf->epc += 4;
            cprintf("Instruction length: 4 bytes (standard instruction)\n");
        }
    }
    break;

case CAUSE_BREAKPOINT:
    //断点异常处理
    /* LAB3 CHALLLENGE3   2311208 :  */
    /*(1)输出指令异常类型（ breakpoint）
     *(2)输出异常指令地址
     *(3)更新 tf->epc寄存器
    */
    cprintf("Exception type: Breakpoint\n");
    cprintf("ebreak caught at 0x%08x\n", tf->epc);

    // 检查指令长度，如果是压缩指令（16位）则+2，否则+4
    {
        unsigned short *instr = (unsigned short*)tf->epc;
        unsigned short instruction = *instr;

        if ((instruction & 0x3) != 0x3) {
            // 压缩指令 (16位)
            cprintf("Instruction word: 0x%04x\n", instruction);
            tf->epc += 2;
            cprintf("Instruction length: 2 bytes (compressed instruction)\n");
        } else {
            // 标准指令 (32位)
            unsigned int full_instruction = *(unsigned int*)tf->epc;
            cprintf("Instruction word: 0x%08x\n", full_instruction);
            tf->epc += 4;
            cprintf("Instruction length: 4 bytes (standard instruction)\n");
        }
    }
    break;
```

### 4.2 实现过程说明

#### 4.2.1 异常检测与分发机制

```c
static inline void trap_dispatch(struct trapframe *tf) {
    if ((intptr_t)tf->cause < 0) {
        // 中断
        interrupt_handler(tf);
    } else {
        // 异常
        exception_handler(tf);
    }
}
```

**工作原理**：
- 通过`tf->cause`的符号位判断是中断还是异常
- 负值表示中断，非负值表示异常
- 调用相应的处理函数

#### 4.2.2 指令长度检测算法

```c
unsigned short *instr = (unsigned short*)tf->epc;
unsigned short instruction = *instr;

if ((instruction & 0x3) != 0x3) {
    // 压缩指令 (16位)
    cprintf("Instruction word: 0x%04x\n", instruction);
    tf->epc += 2;
} else {
    // 标准指令 (32位)
    unsigned int full_instruction = *(unsigned int*)tf->epc;
    cprintf("Instruction word: 0x%08x\n", full_instruction);
    tf->epc += 4;
}
```

**算法详解**：
1. **读取指令前2字节**：`*(unsigned short*)tf->epc`
2. **检查低2位**：`instruction & 0x3`
3. **长度判断**：
   - 低2位≠11 → 2字节压缩指令
   - 低2位=11 → 4字节标准指令
4. **格式化输出**：
   - 16位：`0x%04x`格式
   - 32位：`0x%08x`格式

#### 4.2.3 程序计数器调整策略

- **跳过异常指令**：确保不会重复触发同一异常
- **正确步进**：根据实际指令长度调整`tf->epc`
- **恢复执行**：异常处理后从下一条指令继续执行

### 4.3 实验演示与结果分析

#### 4.3.1 测试用例设计

| 测试用例 | 指令编码 | 指令长度 | 预期异常类型 |
|---------|---------|---------|-------------|
| Test 1 | 0x12345677 | 4字节 | Illegal instruction |
| Test 2 | 0x0000 | 2字节 | Illegal instruction |
| Test 3 | 0x9002 | 2字节 | Breakpoint |
| Test 4 | 0x00100073 | 4字节 | Breakpoint |

#### 4.3.2 实验结果分析

![异常处理实验截图](assets/image.png)

**Test 1 - 32位非法指令**：
```
Test 1: Triggering 32-bit illegal instruction...
Exception type: Illegal instruction
Illegal instruction caught at 0xc02000b4
Instruction word: 0x12345677
Instruction length: 4 bytes (standard instruction)
```
- 指令`0x12345677`被正确识别为4字节指令
- 低2位`0x77 & 0x3 = 0x3`，符合标准指令特征
- 程序计数器正确增加4字节

**Test 2 - 16位非法指令**：
```
Test 2: Triggering 16-bit illegal instruction...
Exception type: Illegal instruction
Illegal instruction caught at 0xc02000c4
Instruction word: 0x0000
Instruction length: 2 bytes (compressed instruction)
```
- 指令`0x0000`被正确识别为2字节压缩指令
- 低2位`0x0000 & 0x3 = 0x0`，符合压缩指令特征
- 程序计数器正确增加2字节

**Test 3 - 16位断点**：
```
Test 3: Triggering 16-bit breakpoint...
Exception type: Breakpoint
ebreak caught at 0xc02000d2
Instruction word: 0x9002
Instruction length: 2 bytes (compressed instruction)
```
- `0x9002`是标准的`c.ebreak`压缩指令
- 成功触发断点异常
- 正确识别为2字节指令

**Test 4 - 32位断点**：
```
Test 4: Triggering 32-bit breakpoint...
Exception type: Breakpoint
ebreak caught at 0xc02000e0
Instruction word: 0x00100073
Instruction length: 4 bytes (standard instruction)
```
- `0x00100073`是标准的`ebreak`基础指令
- 成功触发断点异常
- 正确识别为4字节指令

#### 4.3.3 实验结论

1. **指令长度检测准确**：成功区分2字节和4字节指令
2. **异常类型识别正确**：准确识别非法指令和断点异常
3. **程序计数器调整恰当**：根据指令长度正确调整执行流
4. **输出信息完整**：提供详细的异常诊断信息

### 4.4 问题回答

#### 4.4.1 异常触发时机问题

**问题**：非法指令可以加在任意位置，但是要注意什么时候异常触发了才会被处理？

**回答**：

异常要能被正确处理，必须满足以下条件：

1. **中断系统已启用**
```c
intr_enable();  // 启用中断和异常处理
```

2. **异常向量已设置**
```c
idt_init();     // 设置stvec指向__alltraps
```

3. **异常处理函数已实现**
```c
// 在exception_handler()中有对应的case分支
case CAUSE_ILLEGAL_INSTRUCTION:
    // 处理代码
```

**异常触发时机**：
异常触发的时机遵循以下流程：
1. **指令执行时刻**：CPU实际执行到异常指令时
2. **硬件响应时刻**：CPU识别异常后的硬件自动处理
3. **软件处理时刻**：操作系统接管后的软件处理

#### 4.4.2 异常类型判断方法

**问题**：查阅参考资料，判断自己触发的异常属于什么类型的，在相应的情况下进行代码修改。

**回答**：

**RISC-V异常编码标准**：
根据RISC-V规范，异常类型通过`scause`寄存器的值标识：

```c
// 在riscv.h中定义的异常编码
#define CAUSE_MISALIGNED_FETCH  0x0   // 指令地址不对齐
#define CAUSE_FAULT_FETCH      0x1   // 指令访问错误
#define CAUSE_ILLEGAL_INSTRUCTION 0x2 // 非法指令
#define CAUSE_BREAKPOINT        0x3   // 断点
#define CAUSE_MISALIGNED_LOAD   0x4   // 加载地址不对齐
#define CAUSE_FAULT_LOAD       0x5   // 加载访问错误
#define CAUSE_MISALIGNED_STORE  0x6   // 存储地址不对齐
#define CAUSE_FAULT_STORE      0x7   // 存储访问错误
#define CAUSE_USER_ECALL       0x8   // 用户态系统调用
#define CAUSE_SUPERVISOR_ECALL 0x9   // 管态系统调用
```

**异常类型识别方法**：
```c
// 在异常处理函数中
switch (tf->cause) {
    case CAUSE_ILLEGAL_INSTRUCTION:
        // 处理非法指令异常
        break;
    case CAUSE_BREAKPOINT:
        // 处理断点异常
        break;
}
```
