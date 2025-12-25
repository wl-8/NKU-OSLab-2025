#ifndef __KERN_SYNC_MONITOR_CONDVAR_H__
#define __KERN_SYNC_MOINTOR_CONDVAR_H__

#include <sem.h>
/* 在 [OS 概念] 7.7 章节中介绍了管程的准确定义和近似实现。
 * 介绍：
 *  管程由 C. A. R. Hoare 和 Per Brinch Hansen 发明，首次在 Brinch Hansen 的
 *  Concurrent Pascal 语言中实现。通常，管程是一个语言结构，编译器通常会强制互斥。
 *  与通常是操作系统结构的信号量相比较。
 * 定义与特征：
 *  管程是一个由多个过程、变量和数据结构组成的集合。
 *  进程可以调用管程的过程，但不能直接访问内部数据结构。
 *  同一时间只有一个进程可以在管程中活动。
 *  条件变量允许进程阻塞和解除阻塞。
 *     cv.wait() 阻塞一个进程。
 *        该进程被称为在等待（或正在等待）条件变量 cv。
 *     cv.signal()（也叫 cv.notify）解除等待条件变量 cv 的进程的阻塞。
 *        当这种情况发生时，我们仍然需要保证只有一个进程在管程中活动。可以有几种方式做到：
 *            在某些系统中，旧进程（执行 signal 的）离开管程，新进程进入
 *            在某些系统中，signal 必须是在管程中执行的最后一条语句
 *            在某些系统中，旧进程将阻塞直到管程再次可用
 *            在某些系统中，新进程（被 signal 解除阻塞的）将保持阻塞直到管程可用
 *   如果一个条件变量被 signal，但没有人在等待，则该 signal 将被丢弃。
 *   与信号量相比，信号量的 signal 将允许将来执行 wait 的进程不被阻塞。
 *   你不应该将条件变量看作传统意义上的变量。
 *     它没有值。
 *     将其看作面向对象意义上的对象。
 *     它有两个方法，wait 和 signal，用于操纵调用进程。
 * 实现：
 *   monitor mt {
 *     ----------------变量------------------
 *     semaphore mutex;         // 互斥信号量
 *     semaphore next;          // next 信号量
 *     int next_count;          // 等待进入管程的进程计数
 *     condvar {int count, sempahore sem}  cv[N];  // 条件变量数组
 *     other variables in mt;
 *     --------条件变量 wait/signal---------------
 *     cond_wait (cv) {         // 等待条件
 *         cv.count ++;         // 等待计数加1
 *         if(mt.next_count>0)  // 如果有进程在 next 队列
 *            signal(mt.next)   // 唤醒 next 队列中的进程
 *         else
 *            signal(mt.mutex); // 否则释放互斥信号量
 *         wait(cv.sem);        // 在条件信号量上等待
 *         cv.count --;         // 等待计数减1
 *      }
 *
 *      cond_signal(cv) {       // 通知条件
 *          if(cv.count>0) {    // 如果有进程在等待该条件
 *             mt.next_count ++;          // next 队列计数加1
 *             signal(cv.sem);            // 唤醒等待该条件的进程
 *             wait(mt.next);             // 当前进程在 next 队列等待
 *             mt.next_count--;           // next 队列计数减1
 *          }
 *       }
 *     --------管程内的例程---------------
 *     routineA_in_mt () {      // 管程内的例程A
 *        wait(mt.mutex);       // 进入管程（获得互斥锁）
 *        ...
 *        real body of routineA // 例程A的实际代码
 *        ...
 *        if(next_count>0)      // 离开管程时
 *            signal(mt.next);  // 如果有进程在等待，唤醒它
 *        else
 *            signal(mt.mutex); // 否则释放互斥锁
 *     }
 */

typedef struct monitor monitor_t;

typedef struct condvar{
    semaphore_t sem;        // 条件变量的信号量，用于让等待的进程睡眠，signal 进程应唤醒等待进程
    int count;              // 等待该条件变量的进程计数
    monitor_t * owner;      // 该条件变量所属的管程
} condvar_t; // 条件变量结构体

typedef struct monitor{
    semaphore_t mutex;      // 互斥锁，用于进入管程内的例程，初值应为 1
    semaphore_t next;       // next 信号量，用于让 signal 的进程自己睡眠，
                            // 其他被唤醒的等待进程或离开管程的进程应唤醒这个睡眠的 signal 进程
    int next_count;         // 等待在 next 信号量上的 signal 进程计数
    condvar_t *cv;          // 管程内的条件变量数组
} monitor_t; // 管程结构体

// 初始化管程中的变量
void     monitor_init (monitor_t *cvp, size_t num_cv);
// 释放管程中的变量
void     monitor_free (monitor_t *cvp, size_t num_cv);
// 唤醒在条件变量上等待的一个进程
void     cond_signal (condvar_t *cvp);
// 让当前进程在条件变量上等待
// 同时原子地释放管程的互斥锁
// 当被唤醒后，重新获得管程的互斥锁
void     cond_wait (condvar_t *cvp);
     
#endif /* !__KERN_SYNC_MONITOR_CONDVAR_H__ */
