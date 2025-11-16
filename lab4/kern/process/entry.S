.text  // 代码段开始
.globl kernel_thread_entry  // 声明 kernel_thread_entry 为全局符号，可被外部文件调用
kernel_thread_entry:        # void kernel_thread(void)
                            # 内核线程入口函数
                            # 功能：作为所有内核线程的统一入口点，执行线程主函数并处理退出
	move a0, s1  # 将参数从 s1 寄存器移动到 a0 寄存器
	             # s1 中保存的是传递给内核线程主函数的参数
	             # RISC-V 调用约定中，a0 是第一个参数寄存器
	jalr s0      # 跳转并链接到 s0 寄存器中的地址
	             # s0 中保存的是内核线程主函数的地址
	             # jalr 指令会将返回地址保存到 ra 寄存器中，然后跳转执行

	jal do_exit  # 调用 do_exit 函数终止当前线程
	             # 当内核线程主函数返回后，自动调用 do_exit 清理资源并退出线程
	             # 这确保了即使线程主函数忘记调用退出函数，线程也能正常退出
