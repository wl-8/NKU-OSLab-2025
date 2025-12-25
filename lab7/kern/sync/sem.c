#include <defs.h>
#include <wait.h>
#include <atomic.h>
#include <kmalloc.h>
#include <sem.h>
#include <proc.h>
#include <sync.h>
#include <assert.h>

void
sem_init(semaphore_t *sem, int value) {
    sem->value = value;
    wait_queue_init(&(sem->wait_queue));
}

static __noinline void __up(semaphore_t *sem, uint32_t wait_state) {
    bool intr_flag;
    local_intr_save(intr_flag); // 关中断
    {
        wait_t *wait;
        if ((wait = wait_queue_first(&(sem->wait_queue))) == NULL) { // 如果没有等待该信号量的进程
            sem->value ++; // 增加信号量的可用数量
        }
        else {
            assert(wait->proc->wait_state == wait_state); // 确保等待该信号量的进程是以正确的等待状态在等待
            wakeup_wait(&(sem->wait_queue), wait, wait_state, 1); // 唤醒等待队列上的第一个等待该信号量的进程
        }
    }
    local_intr_restore(intr_flag); // 开中断
}

static __noinline uint32_t __down(semaphore_t *sem, uint32_t wait_state) {
    bool intr_flag;
    local_intr_save(intr_flag); // 关中断
    if (sem->value > 0) { // 如果信号量可用
        sem->value --; // 占用信号量
        local_intr_restore(intr_flag); // 开中断
        return 0; // 成功获得信号量，返回0
    }
    wait_t __wait, *wait = &__wait;
    wait_current_set(&(sem->wait_queue), wait, wait_state); // 当前进程等待该信号量（加入等待队列，状态改为睡眠）
    local_intr_restore(intr_flag); // 开中断

    schedule(); // 调度其他进程运行，当前进程保持睡眠状态，直到被唤醒

    local_intr_save(intr_flag); // 关中断
    wait_current_del(&(sem->wait_queue), wait); // 从等待队列中删除当前进程的等待项
    local_intr_restore(intr_flag); // 开中断

    if (wait->wakeup_flags != wait_state) { // 不经过正常up唤醒
        return wait->wakeup_flags;
    }
    return 0;
}

void
up(semaphore_t *sem) {
    __up(sem, WT_KSEM);
}

void
down(semaphore_t *sem) {
    uint32_t flags = __down(sem, WT_KSEM);
    assert(flags == 0);
}

bool
try_down(semaphore_t *sem) {
    bool intr_flag, ret = 0;
    local_intr_save(intr_flag);
    if (sem->value > 0) {
        sem->value --, ret = 1;
    }
    local_intr_restore(intr_flag);
    return ret;
}

