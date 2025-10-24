#include <clock.h>
#include <console.h>
#include <defs.h>
#include <intr.h>
#include <kdebug.h>
#include <kmonitor.h>
#include <pmm.h>
#include <stdio.h>
#include <string.h>
#include <trap.h>
#include <dtb.h>

int kern_init(void) __attribute__((noreturn));
void grade_backtrace(void);

int kern_init(void) {
    extern char edata[], end[];
    // 先清零 BSS，再读取并保存 DTB 的内存信息，避免被清零覆盖（为了解释变化 正式上传时我觉得应该删去这句话）
    memset(edata, 0, end - edata);
    dtb_init();
    cons_init();  // init the console
    const char *message = "(THU.CST) os is loading ...\0";
    //cprintf("%s\n\n", message);
    cputs(message);

    print_kerninfo();

    // grade_backtrace();
    idt_init();  // init interrupt descriptor table

    pmm_init();  // init physical memory management

    idt_init();  // init interrupt descriptor table

    clock_init();   // init clock interrupt
    intr_enable();  // enable irq interrupt

    // 测试Challenge3：异常中断处理
    cprintf("\n=== Testing Exception Handling (Challenge3) ===\n");

    // 测试1：32位非法指令
    cprintf("Test 1: Triggering 32-bit illegal instruction...\n");
    asm volatile(".word 0x12345677");

    // 测试2：16位非法指令
    cprintf("Test 2: Triggering 16-bit illegal instruction...\n");
    asm volatile(".2byte 0x0000");

    // 测试3：16位断点
    cprintf("Test 3: Triggering 16-bit breakpoint...\n");
    asm volatile(".2byte 0x9002"); // 16位断点 (0x9002是c.ebreak的压缩指令)

    // 测试4：32位断点
    cprintf("Test 4: Triggering 32-bit breakpoint...\n");
    asm volatile(".word 0x00100073"); // 32位断点 (0x00100073是标准ebreak指令)

    cprintf("=== Exception handling tests completed ===\n\n");

    /* do nothing */
    while (1)
        ;
}

void __attribute__((noinline))
grade_backtrace2(int arg0, int arg1, int arg2, int arg3) {
    mon_backtrace(0, NULL, NULL);
}

void __attribute__((noinline)) grade_backtrace1(int arg0, int arg1) {
    grade_backtrace2(arg0, (uintptr_t)&arg0, arg1, (uintptr_t)&arg1);
}

void __attribute__((noinline)) grade_backtrace0(int arg0, int arg1, int arg2) {
    grade_backtrace1(arg0, arg2);
}

void grade_backtrace(void) { grade_backtrace0(0, (uintptr_t)kern_init, 0xffff0000); }

