#include <stdio.h>
#include <ulib.h>
#include <string.h>

// 定义一个全局变量，用于测试数据共享和隔离
volatile int shared_data = 100;

int main(void) {
    int pid;
    
    cprintf("COW Test Start: shared_data = %d\n", shared_data);

    // 创建子进程
    pid = fork();

    if (pid == 0) {
        // 子进程执行路径
        cprintf("Child: I am child process. Reading shared_data = %d\n", shared_data);
        
        // 此时父子进程应该共享同一物理页，读取操作不应触发 Page Fault（或者说不触发 COW 复制）
        
        cprintf("Child: Now I am going to modify shared_data...\n");
        // 这一步写操作应该触发 Store Page Fault，内核执行 COW，复制页面
        shared_data = 200;
        
        cprintf("Child: Modified shared_data = %d\n", shared_data);
        cprintf("Child: Exiting...\n");
        exit(0);
    } else {
        // 父进程执行路径
        assert(pid > 0);
        
        // 等待子进程结束，确保子进程已经完成了修改
        if (wait() != 0) {
            panic("wait failed");
        }
        
        cprintf("Parent: Child has exited.\n");
        
        // 检查父进程的数据是否被子进程修改
        // 如果 COW 正常工作，父进程的 shared_data 应该保持为 100
        // 如果 COW 失败（比如直接共享了物理页且没复制），这里可能会变成 200
        cprintf("Parent: Reading shared_data = %d\n", shared_data);
        
        if (shared_data == 100) {
            cprintf("Parent: shared_data is unchanged. COW works!\n");
            cprintf("COW Test Passed.\n");
        } else {
            cprintf("Parent: shared_data CHANGED to %d! COW failed!\n", shared_data);
            panic("COW Test Failed");
        }
    }
    return 0;
}