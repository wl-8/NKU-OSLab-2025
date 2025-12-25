#include <stdio.h>
#include <proc.h>
#include <sem.h>
#include <monitor.h>
#include <assert.h>

#define N 5 /* 哲学家数目 */
#define LEFT (i-1+N)%N /* i的左邻号码 */
#define RIGHT (i+1)%N /* i的右邻号码 */
#define THINKING 0 /* 哲学家正在思考 */
#define HUNGRY 1 /* 哲学家想取得叉子 */
#define EATING 2 /* 哲学家正在吃面 */
#define TIMES  4 /* 吃4次饭 */
#define SLEEP_TIME 10

//-----------------使用信号量解决哲学家进餐问题 --------
/*伪代码：使用信号量解决哲学家进餐问题
系统 DINING_PHILOSOPHERS

变量：
me:    信号量，初值为 1;                    # 用于互斥
s[5]:  信号量 s[5]，初值为 0;               # 用于同步
pflag[5]: {THINK, HUNGRY, EAT}，初值 THINK;  # 哲学家标志

# 如前所述，每个哲学家都在思考和吃饭的无限循环中。

过程 philosopher(i)
  {
    循环 直到 TRUE 做
     {
       思考;
       获取两把叉子(i);
       吃饭;
       放下两把叉子(i);
     }
  }

# take_chopsticks 过程涉及检查相邻哲学家的状态，
# 然后声明自己想吃饭的意图。这是一个两阶段协议；
# 首先声明状态为 HUNGRY，然后转向 EAT。

过程 take_chopsticks(i)
  {
    DOWN(me);               # 临界区
    pflag[i] := HUNGRY;
    test[i];
    UP(me);                 # 退出临界区
    DOWN(s[i])              # 如果有资源就吃饭
   }

void test(i)                # 让哲学家[i]吃饭，如果他在等待的话
  {
    if ( pflag[i] == HUNGRY // 哲学家i饿了
      && pflag[i-1] != EAT  // 左邻居没在吃饭
      && pflag[i+1] != EAT) // 右邻居没在吃饭
       then
        {
          pflag[i] := EAT; // 哲学家i开始吃饭
          UP(s[i]) // 让哲学家i吃饭
         }
    }


# 一旦哲学家吃完饭，剩下的就是放下两把叉子，
# 从而释放等待中的邻居。

void drop_chopsticks(int i)
  {
    DOWN(me);                # 临界区
    test(i-1);               # 让左邻居吃饭（如果可能的话）
    test(i+1);               # 让右邻居吃饭（如果可能的话）
    UP(me);                  # 退出临界区
   }

*/
//---------- 使用信号量解决哲学家进餐问题 ----------------------
int state_sema[N]; /* 记录每个人状态的数组 */
/* 信号量是一个特殊的整型变量 */
semaphore_t mutex; /* 临界区互斥 */
semaphore_t s[N]; /* 每个哲学家一个信号量 */

struct proc_struct *philosopher_proc_sema[N];

void phi_test_sema(int i) /* i：哲学家号码从0到N-1 */
{ 
    if(state_sema[i]==HUNGRY&&state_sema[LEFT]!=EATING
            &&state_sema[RIGHT]!=EATING)
    {
        state_sema[i]=EATING;
        up(&s[i]);
    }
}

void phi_take_forks_sema(int i) /* i：哲学家号码从0到N-1 */
{ 
        down(&mutex); /* 进入临界区 */
        state_sema[i]=HUNGRY; /* 记录下哲学家i饥饿的事实 */
        phi_test_sema(i); /* 试图得到两只叉子 */
        up(&mutex); /* 离开临界区 */
        down(&s[i]); /* 如果得不到叉子就阻塞 */
        // 笔记：这里一定要先离开临界区再down(s[i])，否则一旦哲学家阻塞，其他哲学家就进不来临界区了
}

void phi_put_forks_sema(int i) /* i：哲学家号码从0到N-1 */
{ 
        down(&mutex); /* 进入临界区 */
        state_sema[i]=THINKING; /* 哲学家进餐结束 */
        phi_test_sema(LEFT); /* 看一下左邻居现在是否能进餐 */
        phi_test_sema(RIGHT); /* 看一下右邻居现在是否能进餐 */
        up(&mutex); /* 离开临界区 */
}

int philosopher_using_semaphore(void * arg) /* i：哲学家号码，从0到N-1 */
{
    int i, iter=0;
    i=(int)arg;
    cprintf("I am No.%d philosopher_sema\n",i);
    while(iter++<TIMES)
    { /* 无限循环 */
        cprintf("Iter %d, No.%d philosopher_sema is thinking\n",iter,i); /* 哲学家正在思考 */
        do_sleep(SLEEP_TIME);
        phi_take_forks_sema(i); 
        /* 需要两只叉子，或者阻塞 */
        cprintf("Iter %d, No.%d philosopher_sema is eating\n",iter,i); /* 进餐 */
        do_sleep(SLEEP_TIME);
        phi_put_forks_sema(i); 
        /* 把两把叉子同时放回桌子 */
    }
    cprintf("No.%d philosopher_sema quit\n",i);
    return 0;    
}

//-----------------使用管程解决哲学家进餐问题 --------
/*伪代码：使用管程解决哲学家进餐问题
 * 管程 dp
 * {
 *  enum {思考, 饥饿, 吃饭} 状态[5];
 *  条件变量 self[5];
 *
 *  void 获取叉子(int i) {
 *      状态[i] = 饥饿;
 *      if ((状态[(i+4)%5] != 吃饭) && (状态[(i+1)%5] != 吃饭)) {
 *        状态[i] = 吃饭;
 *      else
 *         self[i].等待();
 *   }
 *
 *   void 放下叉子(int i) {
 *      状态[i] = 思考;
 *      if ((状态[(i+4)%5] == 饥饿) && (状态[(i+3)%5] != 吃饭)) {
 *          状态[(i+4)%5] = 吃饭;
 *          self[(i+4)%5].通知();
 *      }
 *      if ((状态[(i+1)%5] == 饥饿) && (状态[(i+2)%5] != 吃饭)) {
 *          状态[(i+1)%5] = 吃饭;
 *          self[(i+1)%5].通知();
 *      }
 *   }
 *
 *   void 初始化() {
 *      for (int i = 0; i < 5; i++)
 *         状态[i] = 思考;
 *   }
 * }
 */

struct proc_struct *philosopher_proc_condvar[N]; // N 个哲学家
int state_condvar[N];                            // 哲学家的状态: 吃饭, 饥饿, 思考
monitor_t mt, *mtp=&mt;                          // 管程

void phi_test_condvar (int i) { 
    if(state_condvar[i]==HUNGRY&&state_condvar[LEFT]!=EATING
            &&state_condvar[RIGHT]!=EATING) {
        cprintf("phi_test_condvar: state_condvar[%d] will eating\n",i);
        state_condvar[i] = EATING ;
        cprintf("phi_test_condvar: signal self_cv[%d] \n",i);
        cond_signal(&mtp->cv[i]) ; // 唤醒等待的哲学家
    }
}


void phi_take_forks_condvar(int i) {
     down(&(mtp->mutex));
//--------进入管程内的例程--------------
     // 实验7 练习1: 2311208
     // 我很饿
     // 试图得到叉子
    state_condvar[i]=HUNGRY; /* 记录下哲学家i饥饿的事实 */
    phi_test_condvar(i);
    if(state_condvar[i]!=EATING){
        cond_wait(&mtp->cv[i]); // 如果得不到叉子就阻塞
    }
//--------离开管程内的例程--------------
      if(mtp->next_count>0)
         up(&(mtp->next));
      else
         up(&(mtp->mutex));
}

void phi_put_forks_condvar(int i) {
     down(&(mtp->mutex));

//--------进入管程内的例程--------------
     // 实验7 练习1: 2311208
     // 我吃完了
     // 检查左右邻居
    state_condvar[i]=THINKING; /* 哲学家进餐结束 */
    phi_test_condvar(LEFT); /* 看一下左邻居现在是否能进餐 */
    phi_test_condvar(RIGHT); /* 看一下右邻居现在是否能进餐 */

//--------离开管程内的例程--------------
     if(mtp->next_count>0)
        up(&(mtp->next));
     else
        up(&(mtp->mutex));
}

//---------- 使用管程(条件变量)解决哲学家进餐问题 --------
int philosopher_using_condvar(void * arg) { /* arg 是哲学家编号 0~N-1*/
  
    int i, iter=0;
    i=(int)arg;
    cprintf("I am No.%d philosopher_condvar\n",i);
    while(iter++<TIMES)
    { /* 迭代*/
        cprintf("Iter %d, No.%d philosopher_condvar is thinking\n",iter,i); /* 思考*/
        do_sleep(SLEEP_TIME);
        phi_take_forks_condvar(i);
        /* 需要两把叉子，可能会被阻塞 */
        cprintf("Iter %d, No.%d philosopher_condvar is eating\n",iter,i); /* 吃饭*/
        do_sleep(SLEEP_TIME);
        phi_put_forks_condvar(i);
        /* 放回两把叉子 */
    }
    cprintf("No.%d philosopher_condvar quit\n",i);
    return 0;    
}

void check_sync(void){

    int i, pids[N];

    //检查信号量
    sem_init(&mutex, 1);
    for(i=0;i<N;i++){
        sem_init(&s[i], 0);
        int pid = kernel_thread(philosopher_using_semaphore, (void *)i, 0);
        if (pid <= 0) {
            panic("create No.%d philosopher_using_semaphore failed.\n");
        }
        pids[i] = pid;
        philosopher_proc_sema[i] = find_proc(pid);
        set_proc_name(philosopher_proc_sema[i], "philosopher_sema_proc");
    }
    for (i=0;i<N;i++)
        assert(do_wait(pids[i],NULL) == 0);

    //检查条件变量
    monitor_init(&mt, N);
    for(i=0;i<N;i++){
        state_condvar[i]=THINKING;
        int pid = kernel_thread(philosopher_using_condvar, (void *)i, 0);
        if (pid <= 0) {
            panic("create No.%d philosopher_using_condvar failed.\n");
        }
        pids[i] = pid;
        philosopher_proc_condvar[i] = find_proc(pid);
        set_proc_name(philosopher_proc_condvar[i], "philosopher_condvar_proc");
    }
    for (i=0;i<N;i++)
        assert(do_wait(pids[i],NULL) == 0);
    monitor_free(&mt, N);
}
