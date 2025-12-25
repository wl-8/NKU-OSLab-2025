# 2025 操作系统 Lab7 实验报告

## 实验内容
---

### 练习0：填写已有实验

本实验依赖实验2/3/4/5/6。请把你做的实验2/3/4/5/6的代码填入本实验中代码中有"LAB2"/"LAB3"/"LAB4"/"LAB5"/"LAB6"的注释相应部分，并确保编译通过。

### 练习1：理解内核级信号量的实现和基于内核级信号量的哲学家就餐问题

完成练习0后，建议大家比较一下（可用meld等文件diff比较软件）个人完成的lab6和练习0完成后的刚修改的lab7之间的区别，分析了解lab7采用信号量的执行过程。执行make grade，测试用例可以通过，但没有全部得分。

请在实验报告中给出内核级信号量的设计描述，并说明其大致执行流程。

请证明/说明为什么我们给出的信号量实现的哲学家问题不会出现死锁。

请在实验报告中给出给用户态进程/线程提供信号量机制的设计方案，并比较说明给内核级提供信号量机制的异同。

### 练习2：完成内核级条件变量和基于内核级条件变量的哲学家就餐问题

首先掌握管程机制，然后基于信号量实现完成条件变量实现，然后用管程机制实现哲学家就餐问题的解决方案（基于条件变量）。

执行：make grade。如果所显示的应用程序检测都输出ok，则基本正确。

请在实验报告中给出内核级条件变量的设计描述，并说明其大致执行流程。

请在实验报告中给出给用户态进程/线程提供条件变量机制的设计方案，并比较说明给内核级提供条件变量机制的异同。

请在实验报告中回答：能否不用基于信号量机制来完成条件变量？如果不能，请给出理由，如果能，请给出设计说明和具体实现。

## 实验过程
---

### 练习1：理解内核级信号量的实现

#### 1.1 内核级信号量的设计描述

##### 1.1.1 信号量数据结构

```c
typedef struct {
    int value;                  // 信号量值
    wait_queue_t wait_queue;    // 等待队列
} semaphore_t;
```

信号量的核心设计思想：
- `value > 0`：表示有 value 个资源可用
- `value = 0`：表示没有资源可用
- `value < 0`：表示有 |value| 个进程在等待该资源

##### 1.1.2 信号量的两个基本操作

**down操作（P操作）- 申请资源：**

```c
static __noinline uint32_t __down(semaphore_t *sem, uint32_t wait_state) {
    bool intr_flag;
    local_intr_save(intr_flag);     // 关中断保护临界区

    if (sem->value > 0) {            // 资源可用
        sem->value--;                // 获得资源
        local_intr_restore(intr_flag);
        return 0;
    }

    // 资源不可用，进程进入睡眠
    wait_t __wait, *wait = &__wait;
    wait_current_set(&(sem->wait_queue), wait, wait_state);  // 加入等待队列
    local_intr_restore(intr_flag);   // 开中断

    schedule();                      // 调度其他进程

    // 被 up() 唤醒后从这里继续
    local_intr_save(intr_flag);
    wait_current_del(&(sem->wait_queue), wait);
    local_intr_restore(intr_flag);

    if (wait->wakeup_flags != wait_state) {
        return wait->wakeup_flags;
    }
    return 0;
}
```

down操作的执行流程：
1. 关中断，保护临界区
2. 检查信号量值
   - 若 value > 0：value 减1，返回
   - 若 value ≤ 0：进程加入等待队列，开中断，调度让出CPU
3. 被唤醒后，从等待队列删除，恢复中断

**up操作（V操作）- 释放资源：**

```c
static __noinline void __up(semaphore_t *sem, uint32_t wait_state) {
    bool intr_flag;
    local_intr_save(intr_flag);

    wait_t *wait;
    if ((wait = wait_queue_first(&(sem->wait_queue))) == NULL) {
        // 没有进程在等待，增加资源计数
        sem->value++;
    } else {
        // 有进程在等待，优先唤醒第一个
        assert(wait->proc->wait_state == wait_state);
        wakeup_wait(&(sem->wait_queue), wait, wait_state, 1);
    }

    local_intr_restore(intr_flag);
}
```

up操作的执行流程：
1. 关中断
2. 检查等待队列
   - 若队列非空：唤醒第一个等待的进程
   - 若队列为空：value 加1
3. 开中断

#### 1.2 哲学家就餐问题的信号量实现

##### 1.2.1 问题分析

哲学家就餐问题需要解决：
- 5个哲学家，5根筷子，围成一圈
- 每个哲学家需要两根筷子（左和右）才能吃饭
- 避免死锁

##### 1.2.2 关键数据结构

```c
#define N 5
#define LEFT (i-1+N)%N
#define RIGHT (i+1)%N

semaphore_t mutex;      // 互斥锁，初值为1
semaphore_t s[N];       // 5个信号量，初值为0
int state[N];           // 状态数组：THINKING, HUNGRY, EATING
```

##### 1.2.3 核心算法

**取筷子：**

```c
void phi_take_forks_sema(int i) {
    down(&mutex);               // 进入临界区
    state[i] = HUNGRY;          // 宣布自己很饿
    phi_test_sema(i);           // 检查能否吃饭
    up(&mutex);                 // 离开临界区
    down(&s[i]);                // 如果不能吃，睡眠在这里
}
```

**放筷子：**

```c
void phi_put_forks_sema(int i) {
    down(&mutex);               // 进入临界区
    state[i] = THINKING;        // 宣布不吃了
    phi_test_sema(LEFT);        // 检查左邻是否能吃
    phi_test_sema(RIGHT);       // 检查右邻是否能吃
    up(&mutex);                 // 离开临界区
}
```

**状态检查：**

```c
void phi_test_sema(int i) {
    if (state[i] == HUNGRY &&
        state[LEFT] != EATING &&
        state[RIGHT] != EATING) {
        state[i] = EATING;
        up(&s[i]);              // 如果等待，则唤醒
    }
}
```

#### 1.3 防死锁分析

##### 1.3.1 死锁的四个必要条件

1. 互斥性：筷子同一时刻只能被一个哲学家使用
2. 占有并等待：哲学家可能持有一根筷子等待另一根
3. 不可抢占：筷子不能被抢走
4. 循环等待：形成等待链

##### 1.3.2 为什么不会死锁

关键在于**集中式的状态检查**：

1. 所有状态修改都在 mutex 保护的临界区内进行
2. phi_test() 中"检查"和"设置"是原子操作
3. 哲学家在检查时，会同时查看邻居的状态

**死锁分析：**

假设所有哲学家都处于 HUNGRY 状态：
- 每个哲学家进入临界区检查邻居
- 最多只有1个哲学家满足"两个邻居都不在吃"的条件
- 该哲学家改变为 EATING，其他4个进入等待队列
- 当第一个哲学家吃完后，他会唤醒邻居
- 形成"一个吃→一个等→一个唤醒"的循环

由于5个哲学家的圆形排列特性，不可能形成循环等待链，因此不会死锁。

#### 1.4 用户态信号量设计方案

##### 1.4.1 系统调用接口

```c
// 创建信号量
int sem_create(const char *name, int initial_value);

// P操作
int sem_wait(int sem_id);

// V操作
int sem_signal(int sem_id);

// 销毁信号量
int sem_destroy(int sem_id);
```

##### 1.4.2 内核实现框架

```c
typedef struct {
    int value;
    wait_queue_t wait_queue;
    pid_t creator;              // 创建者PID
    char name[32];              // 信号量名称
    int ref_count;              // 引用计数
} kernel_semaphore_t;

// 用户程序中的包装
#include <unistd.h>
int sem_create(const char *name, int initial_value) {
    return syscall(SYS_sem_create, name, initial_value);
}

int sem_wait(int sem_id) {
    return syscall(SYS_sem_wait, sem_id);
}

int sem_signal(int sem_id) {
    return syscall(SYS_sem_signal, sem_id);
}
```

##### 1.4.3 内核级vs用户态信号量对比

| 特性 | 内核级 | 用户态 |
|------|--------|--------|
| 实现位置 | 内核中直接实现 | 通过系统调用 |
| 性能 | 高（直接） | 中等（系统调用开销） |
| 跨进程 | 原生支持 | 需要命名或共享 |
| 资源管理 | 自动清理 | 需要显式管理 |
| 安全性 | 高（内核保护） | 需要额外验证 |
| 易用性 | 简单 | 相对复杂 |

---

### 练习2：条件变量的实现

#### 2.1 管程的基本概念

##### 2.1.1 管程的定义

管程是一种**高级同步原语**，将以下三个要素组织在一起：
1. 共享资源变量
2. 对共享资源的操作
3. 同步机制（互斥锁和条件变量）

核心特性：**同一时刻只有一个进程在管程内执行**

##### 2.1.2 管程的数据结构

```c
typedef struct {
    semaphore_t sem;        // 条件变量的信号量
    int count;              // 等待进程计数
    monitor_t *owner;       // 所属管程
} condvar_t;

typedef struct {
    semaphore_t mutex;      // 互斥锁，初值=1
    semaphore_t next;       // next信号量，初值=0
    int next_count;         // next上等待的进程数
    condvar_t *cv;          // 条件变量数组
} monitor_t;
```

#### 2.2 条件变量的两个核心操作

##### 2.2.1 cond_wait() - 等待条件

```c
void cond_wait(condvar_t *cvp) {
    cvp->count++;           // 等待计数加1

    // 释放互斥锁
    if (cvp->owner->next_count > 0) {
        up(&cvp->owner->next);  // 唤醒next上的进程
    } else {
        up(&cvp->owner->mutex); // 释放互斥锁
    }

    down(&cvp->sem);        // 在条件上睡眠

    cvp->count--;           // 等待计数减1
}
```

cond_wait 的执行流程：
1. 等待计数加1，记录一个进程在等待
2. 释放互斥锁（确保其他进程能进入管程）
3. 在条件变量的信号量上睡眠
4. 被唤醒后，自动拥有互斥锁
5. 等待计数减1

##### 2.2.2 cond_signal() - 通知条件（Hoare语义）

```c
void cond_signal(condvar_t *cvp) {
    if (cvp->count > 0) {               // 有进程在等待
        cvp->owner->next_count++;       // signaler阻塞计数加1
        up(&cvp->sem);                  // 唤醒等待的进程
        down(&cvp->owner->next);        // signaler自己睡眠
        cvp->owner->next_count--;       // 被唤醒后计数减1
    }
    // 如果没有进程在等，信号直接丢弃
}
```

cond_signal 的关键特性 - **Hoare语义（Signal-and-Wait）**：
- signaler 唤醒等待进程后，**立即阻塞自己**
- 这保证了管程内同一时刻只有一个进程执行
- 等待进程被唤醒时，自动获得管程的控制权

#### 2.3 管程的使用模板

**【必须按照这个模板写代码】**

```c
void monitor_routine() {
    down(&mtp->mutex);          // 进入管程

    // === 在管程内执行业务逻辑 ===
    // 只有这个进程在执行
    // 可以安全地访问共享变量
    // 可能调用 cond_wait() 或 cond_signal()

    // 离开管程
    if (mtp->next_count > 0)
        up(&mtp->next);         // 唤醒被signal阻塞的进程
    else
        up(&mtp->mutex);        // 释放互斥锁
}
```

#### 2.4 条件变量版本的哲学家问题

##### 2.4.1 数据结构

```c
monitor_t mt;               // 管程
int state[5];               // 哲学家状态
```

##### 2.4.2 取筷子

```c
void phi_take_forks_condvar(int i) {
    down(&mt.mutex);        // 进入管程

    state[i] = HUNGRY;
    phi_test_condvar(i);

    if (state[i] != EATING) {
        cond_wait(&mt.cv[i]);   // 等待条件满足
    }

    if (mt.next_count > 0)
        up(&mt.next);
    else
        up(&mt.mutex);
}
```

##### 2.4.3 放筷子

```c
void phi_put_forks_condvar(int i) {
    down(&mt.mutex);        // 进入管程

    state[i] = THINKING;
    phi_test_condvar(LEFT);
    phi_test_condvar(RIGHT);

    if (mt.next_count > 0)
        up(&mt.next);
    else
        up(&mt.mutex);
}
```

##### 2.4.4 状态检查

```c
void phi_test_condvar(int i) {
    if (state[i] == HUNGRY &&
        state[LEFT] != EATING &&
        state[RIGHT] != EATING) {
        state[i] = EATING;
        cond_signal(&mt.cv[i]);
    }
}
```

#### 2.5 条件变量 vs 信号量对比

| 特性 | 信号量 | 条件变量 |
|------|--------|---------|
| 资源计数 | 是 | 否 |
| signal无人等 | value增加 | 信号丢弃 |
| 加锁位置 | 多处手动 | 一处自动 |
| 所在框架 | 独立 | 必须在管程内 |
| 容易出错 | 高 | 低 |
| 代码复杂度 | 中等 | 较低 |

#### 2.6 用户态条件变量设计方案

##### 2.6.1 用户态API

```c
typedef struct {
    semaphore_t sem;
    int count;
    monitor_t *owner;
} cond_t;

// 等待条件
void cond_wait(cond_t *cond) {
    cond->count++;
    if (cond->owner->next_count > 0)
        sem_up(&cond->owner->next);
    else
        sem_up(&cond->owner->mutex);
    sem_down(&cond->sem);
    cond->count--;
}

// 唤醒一个等待进程
void cond_signal(cond_t *cond) {
    if (cond->count > 0) {
        cond->owner->next_count++;
        sem_up(&cond->sem);
        sem_down(&cond->owner->next);
        cond->owner->next_count--;
    }
}
```

##### 2.6.2 内核级vs用户态条件变量对比

| 特性 | 内核级 | 用户态 |
|------|--------|--------|
| 实现 | 内核完全实现 | 库函数+系统调用 |
| 开销 | 低 | 中（系统调用） |
| 灵活性 | 固定 | 可定制 |
| 安全性 | 高 | 中 |
| 调试 | 容易 | 困难 |

#### 2.7 能否不用信号量实现条件变量

**答案：不能。**

**理由：**

1. **原子性要求**
   ```c
   cond_wait() 必须原子地同时做两件事：
   - 释放互斥锁
   - 进程进入睡眠

   如果分开执行，会有竞态条件：
   up(&mutex);     // 释放锁
   // 【问题】这里另一个进程可能修改条件
   sleep();        // 进程可能永久睡眠，再也无法被唤醒
   ```

2. **信号的实现**
   - 需要一种机制来"记住"有进程在等待（count字段）
   - 需要一种机制来唤醒等待的进程（sem字段）
   - 这正是信号量的核心功能

3. **Hoare语义的实现**
   - signaler 需要阻塞等待（down(&next)）
   - 被唤醒的进程需要原子地重新获得互斥锁
   - 这都依赖于信号量的原子性保证

**结论：** 条件变量本质上是对信号量的封装和组织，不可能脱离信号量实现。两者的关系是"分层"而非"替代"。
