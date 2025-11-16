#include <riscv.h>  // 包含 RISC-V 架构相关的宏定义

.text  // 代码段开始

# void switch_to(struct proc_struct* from, struct proc_struct* to)
# 进程上下文切换函数
# 功能：将 CPU 的执行上下文从一个进程切换到另一个进程
# 参数：a0 = from 进程的 proc_struct 指针，a1 = to 进程的 proc_struct 指针
.globl switch_to  // 声明 switch_to 为全局符号，可被外部文件调用
switch_to:
    # save from's registers
    # 保存当前进程（from）的寄存器状态到其进程控制块中
    # 这些寄存器需要保存，因为在进程切换后需要恢复现场
    
    STORE ra, 0*REGBYTES(a0)    # 保存返回地址寄存器（return address）
    STORE sp, 1*REGBYTES(a0)    # 保存栈指针寄存器（stack pointer）
    STORE s0, 2*REGBYTES(a0)    # 保存被调用者保存寄存器 s0（callee-saved register）
    STORE s1, 3*REGBYTES(a0)    # 保存被调用者保存寄存器 s1
    STORE s2, 4*REGBYTES(a0)    # 保存被调用者保存寄存器 s2
    STORE s3, 5*REGBYTES(a0)    # 保存被调用者保存寄存器 s3
    STORE s4, 6*REGBYTES(a0)    # 保存被调用者保存寄存器 s4
    STORE s5, 7*REGBYTES(a0)    # 保存被调用者保存寄存器 s5
    STORE s6, 8*REGBYTES(a0)    # 保存被调用者保存寄存器 s6
    STORE s7, 9*REGBYTES(a0)    # 保存被调用者保存寄存器 s7
    STORE s8, 10*REGBYTES(a0)   # 保存被调用者保存寄存器 s8
    STORE s9, 11*REGBYTES(a0)   # 保存被调用者保存寄存器 s9
    STORE s10, 12*REGBYTES(a0)  # 保存被调用者保存寄存器 s10
    STORE s11, 13*REGBYTES(a0)  # 保存被调用者保存寄存器 s11

    # restore to's registers
    # 恢复目标进程（to）的寄存器状态
    # 从目标进程的进程控制块中读取之前保存的寄存器值
    
    LOAD ra, 0*REGBYTES(a1)     # 恢复目标进程的返回地址寄存器
    LOAD sp, 1*REGBYTES(a1)     # 恢复目标进程的栈指针寄存器
    LOAD s0, 2*REGBYTES(a1)     # 恢复目标进程的被调用者保存寄存器 s0
    LOAD s1, 3*REGBYTES(a1)     # 恢复目标进程的被调用者保存寄存器 s1
    LOAD s2, 4*REGBYTES(a1)     # 恢复目标进程的被调用者保存寄存器 s2
    LOAD s3, 5*REGBYTES(a1)     # 恢复目标进程的被调用者保存寄存器 s3
    LOAD s4, 6*REGBYTES(a1)     # 恢复目标进程的被调用者保存寄存器 s4
    LOAD s5, 7*REGBYTES(a1)     # 恢复目标进程的被调用者保存寄存器 s5
    LOAD s6, 8*REGBYTES(a1)     # 恢复目标进程的被调用者保存寄存器 s6
    LOAD s7, 9*REGBYTES(a1)     # 恢复目标进程的被调用者保存寄存器 s7
    LOAD s8, 10*REGBYTES(a1)    # 恢复目标进程的被调用者保存寄存器 s8
    LOAD s9, 11*REGBYTES(a1)    # 恢复目标进程的被调用者保存寄存器 s9
    LOAD s10, 12*REGBYTES(a1)   # 恢复目标进程的被调用者保存寄存器 s10
    LOAD s11, 13*REGBYTES(a1)   # 恢复目标进程的被调用者保存寄存器 s11

    ret  # 返回到目标进程，此时 ra 寄存器已恢复为目标进程的返回地址
