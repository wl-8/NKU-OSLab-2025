#include <stdio.h>
#include <monitor.h>
#include <kmalloc.h>
#include <assert.h>


// 初始化管程
void
monitor_init (monitor_t * mtp, size_t num_cv) {
    int i;
    assert(num_cv>0);
    mtp->next_count = 0; // 初始化等待在 next 信号量上的 signal 进程计数为0
    mtp->cv = NULL;
    sem_init(&(mtp->mutex), 1); // 初始化互斥锁，初值为1
    sem_init(&(mtp->next), 0); // 初始化 next 信号量，初值为0
    mtp->cv =(condvar_t *) kmalloc(sizeof(condvar_t)*num_cv); // 分配条件变量数组内存
    assert(mtp->cv!=NULL);
    for(i=0; i<num_cv; i++){
        mtp->cv[i].count=0; // 初始化等待该条件变量的进程计数为0
        sem_init(&(mtp->cv[i].sem),0); // 初始化条件变量的信号量，初值为0
        mtp->cv[i].owner=mtp; // 设置该条件变量所属的管程
    }
}

// 释放管程
void
monitor_free (monitor_t * mtp, size_t num_cv) {
    kfree(mtp->cv);
}

// 唤醒在条件变量上等待的一个进程
void
cond_signal (condvar_t *cvp) {
   // 实验7 练习1: 2311208
   cprintf("cond_signal begin: cvp %x, cvp->count %d, cvp->owner->next_count %d\n", cvp, cvp->count, cvp->owner->next_count);
  /*
   *      cond_signal(cv) {
   *          if(cv.count>0) {
   *             mt.next_count ++;
   *             signal(cv.sem);
   *             wait(mt.next);
   *             mt.next_count--;
   *          }
   *       }
   */
    if(cvp->count>0){ // 如果有进程在等待该条件
         cvp->owner->next_count ++; // next 队列计数加1
         up(&(cvp->sem)); // 唤醒等待该条件的进程
         down(&(cvp->owner->next)); // 当前进程在 next 队列等待
         cvp->owner->next_count--; // next 队列计数减1
    }
   cprintf("cond_signal end: cvp %x, cvp->count %d, cvp->owner->next_count %d\n", cvp, cvp->count, cvp->owner->next_count);
}

// 让当前进程在条件变量上等待
// 原子地释放管程的互斥锁
// 当被唤醒后，重新获得管程的互斥锁
// 注意：mtp 是管程的互斥信号量
void
cond_wait (condvar_t *cvp) {
    // 实验7 练习1: 2311208
    cprintf("cond_wait begin:  cvp %x, cvp->count %d, cvp->owner->next_count %d\n", cvp, cvp->count, cvp->owner->next_count);
   /*
    *         cv.count ++;
    *         if(mt.next_count>0)
    *            signal(mt.next)
    *         else
    *            signal(mt.mutex);
    *         wait(cv.sem);
    *         cv.count --;
    */
    cvp->count++; // 等待该条件变量的进程计数加1
    if(cvp->owner->next_count>0){ // 如果有进程在 next 队列
        up(&(cvp->owner->next)); // 唤醒 next 队列中的进程
    } else {
        up(&(cvp->owner->mutex)); // 否则释放互斥信号量
    }
    down(&(cvp->sem)); // 在条件信号量上等待
    cvp->count--; // 等待该条件变量的进程计数减1
    cprintf("cond_wait end:  cvp %x, cvp->count %d, cvp->owner->next_count %d\n", cvp, cvp->count, cvp->owner->next_count);
}
