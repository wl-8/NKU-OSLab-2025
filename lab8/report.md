# 2025 操作系统 Lab8 实验报告

> 团队成员: 2313857陈天祺 & 2311208魏来 & 2312166王旭

## 实验目的

- 了解文件系统抽象层-VFS的设计与实现
- 了解基于索引节点组织方式的Simple FS文件系统与操作的设计与实现
- 了解“一切皆为文件”思想的设备文件设计
- 了解简单系统终端的实现

## 实验内容

### 练习0：填写已有实验

本实验依赖实验2/3/4/5/6/7。请把你做的实验2/3/4/5/6/7的代码填入本实验中代码中有“LAB2”/“LAB3”/“LAB4”/“LAB5”/“LAB6”  /“LAB7”的注释相应部分。并确保编译通过。注意：为了能够正确执行lab8的测试应用程序，可能需对已完成的实验2/3/4/5/6/7的代码进行进一步改进。

### 练习1：完成读文件操作的实现（需要编码）

首先了解打开文件的处理流程，然后参考本实验后续的文件读写操作的过程分析，填写在 kern/fs/sfs/sfs_inode.c中 的sfs_io_nolock()函数，实现读文件中数据的代码。

### 练习2：完成基于文件系统的执行程序机制的实现（需要编码）

改写proc.c中的load_icode函数和其他相关函数，实现基于文件系统的执行程序机制。执行：make qemu。如果能看看到sh用户程序的执行界面，则基本成功了。如果在sh用户界面上可以执行`exit`, `hello`（更多用户程序放在`user`目录下）等其他放置在`sfs`文件系统中的其他执行程序，则可以认为本实验基本成功。

### 扩展练习 Challenge1：完成基于“UNIX的PIPE机制”的设计方案

如果要在ucore里加入UNIX的管道（Pipe）机制，至少需要定义哪些数据结构和接口？（接口给出语义即可，不必具体实现。数据结构的设计应当给出一个（或多个）具体的C语言struct定义。在网络上查找相关的Linux资料和实现，请在实验报告中给出设计实现”UNIX的PIPE机制“的概要设方案，你的设计应当体现出对可能出现的同步互斥问题的处理。）

### 扩展练习 Challenge2：完成基于“UNIX的软连接和硬连接机制”的设计方案

如果要在ucore里加入UNIX的软连接和硬连接机制，至少需要定义哪些数据结构和接口？（接口给出语义即可，不必具体实现。数据结构的设计应当给出一个（或多个）具体的C语言struct定义。在网络上查找相关的Linux资料和实现，请在实验报告中给出设计实现”UNIX的软连接和硬连接机制“的概要设方案，你的设计应当体现出对可能出现的同步互斥问题的处理。）

---

## 练习0：填写已有实验

### 1. `kern/process/proc.c` - 进程管理

#### 1.1 `alloc_proc()` 函数
初始化进程控制块的各个字段，包括：
- 初始化进程状态、pid、内核栈、上下文、页目录等基本字段
- 初始化等待状态和进程关系指针（cptr/yptr/optr）
- 初始化调度相关字段（run_queue、run_link、time_slice、stride调度相关变量）
- 初始化文件结构体指针 `filesp = NULL`

```c
proc->state = PROC_UNINIT;
proc->pid = -1;
proc->runs = 0;
proc->kstack = 0;
proc->need_resched = 0;
proc->parent = NULL;
proc->mm = NULL;
memset(&(proc->context), 0, sizeof(struct context));
proc->tf = NULL;
proc->pgdir = boot_pgdir_pa;
proc->flags = 0;
memset(proc->name, 0, PROC_NAME_LEN);
// LAB5 add:
proc->wait_state = 0;
proc->cptr = proc->optr = proc->yptr = NULL;
// LAB6 add:
proc->rq = NULL;
list_init(&(proc->run_link));
proc->time_slice = 0;
proc->lab6_run_pool.left = proc->lab6_run_pool.right = proc->lab6_run_pool.parent = NULL;
proc->lab6_stride = 0;
proc->lab6_priority = 0;
// LAB8 add:
proc->filesp = NULL;
```

#### 1.2 `proc_run()` 函数 
实现进程切换功能，包括关中断、切换页表、刷新TLB、上下文切换：

```c
if (proc != current) {
    bool intr_flag;
    struct proc_struct *prev = current, *next = proc;
    local_intr_save(intr_flag);
    {
        current = proc;
        lsatp(next->pgdir);
        flush_tlb();  // LAB8 update
        switch_to(&(prev->context), &(next->context));
    }
    local_intr_restore(intr_flag);
}
```

#### 1.3 `do_fork()` 函数
实现进程创建，包括分配PCB、设置内核栈、复制内存映射、设置进程关系等：
- LAB5 更新：设置父进程关系，使用 `set_links()` 建立进程关系
- LAB8 更新：使用 `copy_files()` 复制文件描述符

### 2. `kern/mm/pmm.c` - 物理内存管理

实现内存复制功能函数 `copy_range()`，用于 fork 时复制父进程的内存到子进程：

```c
void *src_kvaddr = page2kva(page);
void *dst_kvaddr = page2kva(npage);
memcpy(dst_kvaddr, src_kvaddr, PGSIZE);
ret = page_insert(to, npage, start, perm);
```

### 3. `kern/schedule/default_sched.c` - 进程调度

实现 Round Robin 进程调度算法

- `RR_init()`: 初始化运行队列
- `RR_enqueue()`: 将进程加入运行队列尾部
- `RR_dequeue()`: 从运行队列移除进程
- `RR_pick_next()`: 选择队首进程运行
- `RR_proc_tick()`: 处理时钟中断，减少时间片

### 4. `kern/sync/monitor.c` - 管程机制 

完成管程的条件变量操作：
- `cond_signal()`: 唤醒等待在条件变量上的一个进程
- `cond_wait()`: 当前进程在条件变量上等待

### 5. `kern/sync/check_sync.c` - 哲学家就餐问题 

使用管程解决哲学家就餐问题：
- `phi_take_forks_condvar()`: 哲学家尝试拿起叉子
- `phi_put_forks_condvar()`: 哲学家放下叉子

---


## 练习1：完成读文件操作的实现

### 1. 文件读取操作的总体流程

在 ucore 中，文件读取操作经过多个层次的抽象和处理，从用户程序调用 `read()` 到实际从磁盘读取数据的完整流程如下：

#### 1.1 用户态到内核态的调用链

```c
用户程序 read(fd, buf, len)
    ↓
用户库函数 sys_read() [user/libs/syscall.c]
    ↓
系统调用 syscall(SYS_read, fd, buf, len)
    ↓
内核系统调用处理 sys_read() [kern/syscall/syscall.c]
    ↓
文件系统接口 sysfile_read() [kern/fs/sysfile.c]
    ↓
VFS层 file_read() [kern/fs/file.c]
    ↓
VFS接口 vop_read() [kern/fs/vfs/inode.h]
    ↓
SFS层 sfs_read() [kern/fs/sfs/sfs_inode.c]
    ↓
SFS核心 sfs_io() [kern/fs/sfs/sfs_inode.c]
    ↓
SFS无锁实现 sfs_io_nolock() [kern/fs/sfs/sfs_inode.c]
    ↓
块设备I/O sfs_rbuf() / sfs_rblock() [kern/fs/sfs/sfs_io.c]
    ↓
设备驱动 ide_read_secs() [kern/driver/ide.c]
```

#### 1.2 核心数据结构

1. **`struct file`**: 文件描述符对应的内核结构，记录文件状态、当前读写位置等
2. **`struct inode`**: VFS层的索引节点抽象，包含文件操作函数表
3. **`struct sfs_inode`**: SFS文件系统的内存inode结构
4. **`struct sfs_disk_inode`**: SFS文件系统的磁盘inode结构
5. **`struct iobuf`**: I/O缓冲区，封装了读写的目标地址、偏移量和长度

### 2. `sfs_io_nolock()` 设计与实现

#### 2.1 函数原型与参数

```c
static int
sfs_io_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, void *buf, 
              off_t offset, size_t *alenp, bool write);
```

- `sfs`: SFS文件系统实例
- `sin`: 读写文件对应的 SFS 内存 inode
- `buf`: 读写数据的内存缓冲区
- `offset`: 文件内的起始偏移量
- `alenp`: 输入为请求的长度，输出为实际读写的长度
- `write`: 标志位，0表示读操作，1表示写操作

#### 2.2 算法设计原理

SFS文件系统将文件数据存储在磁盘块中，每个磁盘块大小为 `SFS_BLKSIZE`（4096字节，与页大小相同）。读取文件时，需要将文件偏移量转换为磁盘块号，然后从磁盘读取数据。

由于读写的起始和结束位置可能不与块边界对齐，因此需要分三种情况处理：

1. **首块部分读取**: 当 `offset` 不是块对齐时，需要从 `offset % SFS_BLKSIZE` 处读到块末尾
2. **中间完整块读取**: 对于中间的完整块，可以一次读取整个块
3. **末块部分读取**: 当 `endpos` 不是块对齐时，需要从块开始读到 `endpos % SFS_BLKSIZE`

#### 2.3 代码实现

```c
static int
sfs_io_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, void *buf, 
              off_t offset, size_t *alenp, bool write) {
    // ... 边界检查和初始化 ...
    
    int ret = 0;
    size_t size, alen = 0;
    uint32_t ino;
    uint32_t blkno = offset / SFS_BLKSIZE;          // 起始块号
    uint32_t nblks = endpos / SFS_BLKSIZE - blkno;  // 完整块数量
    
    // 计算首块内的偏移
    blkoff = offset % SFS_BLKSIZE;
    
    // ===== Case 1: 处理首个非对齐块 =====
    if (blkoff != 0) {
        // 计算首块要读取的字节数
        size = (nblks != 0) ? (SFS_BLKSIZE - blkoff) : (endpos - offset);
        
        // 获取逻辑块号对应的物理块号
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }
        
        // 读取首块的部分数据
        if ((ret = sfs_buf_op(sfs, buf, size, ino, blkoff)) != 0) {
            goto out;
        }
        
        alen += size;
        buf += size;
        blkno++;
        if (nblks > 0) nblks--;
    }
    
    // ===== Case 2: 处理中间的完整块 =====
    while (nblks > 0) {
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }
        
        // 读取完整的一个块
        if ((ret = sfs_block_op(sfs, buf, ino, 1)) != 0) {
            goto out;
        }
        
        alen += SFS_BLKSIZE;
        buf += SFS_BLKSIZE;
        blkno++;
        nblks--;
    }
    
    // ===== Case 3: 处理最后的非对齐块 =====
    size = endpos % SFS_BLKSIZE;
    if (size != 0) {
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }
        
        // 从块开头读取部分数据
        if ((ret = sfs_buf_op(sfs, buf, size, ino, 0)) != 0) {
            goto out;
        }
        
        alen += size;
    }

out:
    *alenp = alen;
    // 如果是写操作且扩展了文件，更新文件大小
    if (offset + alen > sin->din->size) {
        sin->din->size = offset + alen;
        sin->dirty = 1;
    }
    return ret;
}
```

### 3. 核心数据结构

#### 磁盘数据结构

超级块 `sfs_super` 记录整个文件系统的基本信息。
```c
/*
 * 超级块结构 (存储在磁盘第0块)
 * 
 * 超级块是文件系统的"元数据"，记录整个文件系统的基本信息。
 * 文件系统挂载时首先读取超级块来了解文件系统的状态。
 */
struct sfs_super {
    uint32_t magic;                                 /* 魔数，必须等于 SFS_MAGIC，用于验证文件系统类型 */
    uint32_t blocks;                                /* 文件系统的总块数 */
    uint32_t unused_blocks;                         /* 未使用的块数 */
    char info[SFS_MAX_INFO_LEN + 1];                /* 文件系统描述信息字符串 */
};
```

磁盘 inode `sfs_disk_inode` 在磁盘上存储文件的元信息以及文件数据块的索引信息。
```c
/*
 * 磁盘 inode 结构 (存储在磁盘上)
 * 
 * inode（索引节点）是文件系统中最重要的数据结构，每个文件/目录对应一个 inode。
 * 它存储文件的元数据（大小、类型等）和数据块的位置信息。
 * 
 * 数据块索引方式:
 * - direct[0..11]: 12个直接块指针，可直接索引 12 * 4KB = 48KB 数据
 * - indirect: 一级间接块指针，指向一个包含块号的块，
 *             可索引 (4KB/4) * 4KB = 4MB 数据
 * - 总计最大文件大小约 4MB + 48KB
 * 
 * 文件数据块布局示例:
 * 
 *   inode
 * +--------+
 * |direct[0]|---> [数据块0]
 * |direct[1]|---> [数据块1]
 * |  ...    |
 * |direct[11]|--> [数据块11]
 * |indirect |---> [间接块] ---> [块号0][块号1]...[块号1023]
 * +--------+                      |       |
 *                                 v       v
 *                            [数据块] [数据块]
    */
    struct sfs_disk_inode {
        uint32_t size;                                  /* 文件大小（字节数） */
        uint16_t type;                                  /* 文件类型: SFS_TYPE_FILE/DIR/LINK */
        uint16_t nlinks;                                /* 硬链接计数（指向此 inode 的目录项数量） */
        uint32_t blocks;                                /* 已分配的数据块数量 */
        uint32_t direct[SFS_NDIRECT];                   /* 12个直接数据块的块号 */
        uint32_t indirect;                              /* 一级间接块的块号（存储更多数据块号） */
    //    uint32_t db_indirect;                         /* 二级间接块（未实现） */
    //   unused
    };
```

目录项 `sfs_disk_entry` 记录文件名到 inode 编号的映射
```c
/*
 * 目录项结构 (存储在目录文件的数据块中)
 * 
 * 目录本质上是一个特殊文件，其数据内容是一系列目录项。
 * 每个目录项记录一个文件名到 inode 号的映射。
 * 
 * 目录数据块布局:
 * +-----+------------------+
 * | ino | name (256 bytes) |  <- 目录项1
 * +-----+------------------+
 * | ino | name (256 bytes) |  <- 目录项2
 * +-----+------------------+
 * | ... | ...              |
 * +-----+------------------+
 */
struct sfs_disk_entry {
    uint32_t ino;                                   /* inode 编号，通过此编号找到文件的 inode */
    char name[SFS_MAX_FNAME_LEN + 1];               /* 文件名（以 '\0' 结尾） */
};
```

#### 内存数据结构

内存 inode `sfs_inode`，由磁盘 inode 读入内存后封装产生。
```c
/*
 * 内存中的 inode 结构
 * 
 * 当需要访问文件时，从磁盘读取 sfs_disk_inode 并包装成 sfs_inode。
 * sfs_inode 包含磁盘 inode 的指针以及运行时管理所需的额外信息。
 * 
 * 关系图:
 *   VFS inode
 *       |
 *       v (通过 vop_info 获取)
 *   sfs_inode -----> sfs_disk_inode (磁盘数据的内存副本)
 *       |
 *       +---> 链入 sfs_fs 的 inode_list (便于遍历所有已加载的 inode)
 *       +---> 链入 sfs_fs 的 hash_list  (便于按 ino 快速查找)
 */
struct sfs_inode {
    struct sfs_disk_inode *din;                     /* 指向磁盘 inode 的内存副本 */
    uint32_t ino;                                   /* inode 编号（在文件系统中唯一标识此文件） */
    bool dirty;                                     /* 脏标志：true 表示 inode 已修改，需要写回磁盘 */
    int reclaim_count;                              /* 引用计数：降为0时可回收此 inode */
    semaphore_t sem;                                /* 信号量：保护 din 的并发访问 */
    list_entry_t inode_link;                        /* 链表节点：链入 sfs_fs 的 inode_list */
    list_entry_t hash_link;                         /* 哈希链表节点：链入 sfs_fs 的 hash_list */
};

/* 从链表节点获取 sfs_inode 结构体指针的宏 */
#define le2sin(le, member)                          \
    to_struct((le), struct sfs_inode, member)
```

文件系统 `sfs_fs` 统一管理各文件结构。
```c
/*
 * 文件系统结构 (每个挂载的 SFS 分区对应一个)
 * 
 * sfs_fs 是整个 SFS 文件系统的核心管理结构，包含：
 * - 超级块信息
 * - 挂载的设备
 * - 空闲块管理
 * - 已加载的 inode 缓存
 * - 同步所需的信号量
 * 
 * 结构关系图:
 * 
 *   struct fs (VFS 层)
 *       |
 *       v (fs->fs_info)
 *   sfs_fs
 *       |
 *       +---> super (超级块副本)
 *       +---> dev   (块设备)
 *       +---> freemap (空闲位图)
 *       +---> inode_list (所有已加载 inode 的链表)
 *       +---> hash_list[] (inode 哈希表，加速查找)
 */
struct sfs_fs {
    struct sfs_super super;                         /* 超级块的内存副本 */
    struct device *dev;                             /* 此文件系统挂载到的块设备 */
    struct bitmap *freemap;                         /* 空闲块位图：0表示已使用，1表示空闲 */
    bool super_dirty;                               /* true 表示超级块或位图已修改，需要写回 */
    void *sfs_buffer;                               /* 临时缓冲区：用于非块对齐的 I/O 操作 */
    semaphore_t fs_sem;                             /* 文件系统信号量：保护元数据操作 */
    semaphore_t io_sem;                             /* I/O 信号量：保护块设备读写 */
    semaphore_t mutex_sem;                          /* 互斥信号量：保护 link/unlink/rename 操作 */
    list_entry_t inode_list;                        /* 已加载 inode 的链表头 */
    list_entry_t *hash_list;                        /* inode 哈希表数组（加速 ino 查找） */
};
```

---

## 练习2：完成基于文件系统的执行程序机制的实现

### 1. 程序执行机制

在 ucore 中，当用户程序调用 `exec()` 系统调用时，操作系统需要将新程序从文件系统加载到内存中，替换当前进程的地址空间，并开始执行新程序。

调用链如下：

```c
用户程序 exec(path, argv)
    ↓
用户库函数 sys_exec() [user/libs/syscall.c]
    ↓
系统调用 syscall(SYS_exec, path, argc, argv)
    ↓
内核系统调用处理 sys_exec() [kern/syscall/syscall.c]
    ↓
do_execve() [kern/process/proc.c]
    ↓
    ├─ sysfile_open() - 打开可执行文件
    ├─ 清理当前进程的内存映射
    └─ load_icode() - 加载程序到内存 [kern/process/proc.c]
        ↓
        ├─ mm_create() - 创建内存管理结构
        ├─ setup_pgdir() - 创建页目录
        ├─ load_icode_read() - 从文件读取 ELF 头和程序段
        ├─ mm_map() - 建立虚拟内存区域
        ├─ pgdir_alloc_page() - 分配物理页并建立映射
        └─ 设置用户栈和 trapframe
```

### 2. `load_icode()` 函数设计与实现

#### 2.1 函数原型

```c
static int load_icode(int fd, int argc, char **kargv);
```

- `fd`: 可执行文件的文件描述符
- `argc`: 命令行参数个数
- `kargv`: 命令行参数数组

#### 2.2 ELF 文件格式

ELF 是 Unix/Linux 系统的标准可执行文件格式,其结构如下：

```
┌─────────────────────────┐
│      ELF Header         │  elfhdr 结构，包含程序入口等信息
├─────────────────────────┤
│   Program Header Table  │  proghdr 数组，描述各程序段
├─────────────────────────┤
│      .text section      │  代码段
├─────────────────────────┤
│      .data section      │  已初始化数据段
├─────────────────────────┤
│      .bss section       │  未初始化数据段
├─────────────────────────┤
│      ...                │
└─────────────────────────┘
```

数据结构定义：
```c
/* file header */
struct elfhdr {
    uint32_t e_magic;     // must equal ELF_MAGIC
    uint8_t e_elf[12];
    uint16_t e_type;      // 1=relocatable, 2=executable, 3=shared object, 4=core image
    uint16_t e_machine;   // 3=x86, 4=68K, etc.
    uint32_t e_version;   // file version, always 1
    uint64_t e_entry;     // 程序入口地址
    uint64_t e_phoff;     // 程序头表的文件偏移
    uint64_t e_shoff;     // file position of section header or 0
    uint32_t e_flags;     // architecture-specific flags, usually 0
    uint16_t e_ehsize;    // size of this elf header
    uint16_t e_phentsize; // size of an entry in program header
    uint16_t e_phnum;     // 程序头表的条目数
    uint16_t e_shentsize; // size of an entry in section header
    uint16_t e_shnum;     // number of entries in section header or 0
    uint16_t e_shstrndx;  // section number that contains section name strings
};

/* program section header */
struct proghdr {
    uint32_t p_type;   // 段类型（PT_LOAD 表示可加载段）
    uint32_t p_flags;  // 段权限（读/写/执行）
    uint64_t p_offset; // 段在文件中的偏移
    uint64_t p_va;     // 段在内存中的虚拟地址
    uint64_t p_pa;     // physical address, not used
    uint64_t p_filesz; // 段在文件中的大小
    uint64_t p_memsz;  // 段在内存中的大小
    uint64_t p_align;  // required alignment, invariably hardware page size
};
```

#### 2.3 实现步骤详解

**步骤1：创建内存管理结构**

```c
if ((mm = mm_create()) == NULL) {
    goto bad_mm;
}
```

为新程序创建一个空的 `mm_struct` 结构，用于管理进程的虚拟内存区域。

**步骤2：创建页目录表**

```c
if (setup_pgdir(mm) != 0) {
    goto bad_pgdir_cleanup_mm;
}
```

分配一页物理内存作为页目录，并复制内核页表项，确保新进程能够访问内核空间。

**步骤3：读取并解析 ELF 文件头**

```c
struct elfhdr elf_header;
if ((ret = load_icode_read(fd, &elf_header, sizeof(struct elfhdr), 0)) != 0) {
    goto bad_elf_cleanup_pgdir;
}
if (elf_header.e_magic != ELF_MAGIC) {
    ret = -E_INVAL_ELF;
    goto bad_elf_cleanup_pgdir;
}
```

从文件开头读取 ELF 头，并验证魔数是否正确。

**步骤4：遍历程序头，加载各段**

对于每个可加载段 (`PT_LOAD`)：

1. **建立虚拟内存映射**：根据段的权限标志设置 VMA 和 PTE 权限

```c
vm_flags = 0;
perm = PTE_U;  // 用户态可访问
if (ph->p_flags & ELF_PF_X) {
    vm_flags |= VM_EXEC;
    perm |= PTE_X;  // 可执行权限
}
if (ph->p_flags & ELF_PF_W) {
    vm_flags |= VM_WRITE;
    perm |= PTE_W;  // 可写权限
}
if (ph->p_flags & ELF_PF_R) {
    vm_flags |= VM_READ;
    perm |= PTE_R;  // 可读权限
}
mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL);
```

2. **分配物理页并加载代码/数据**：

```c
while (start < end) {
    page = pgdir_alloc_page(mm->pgdir, la, perm);
    load_icode_read(fd, page2kva(page) + off, size, offset);
    // ...
}
```

3. **处理 BSS 段**：BSS 段在文件中不占空间，但需要在内存中分配并清零

```c
if (ph->p_memsz > ph->p_filesz) {
    // 清零 BSS 区域
    memset(page2kva(page) + off, 0, size);
}
```

**步骤5：设置用户栈**

```c
vm_flags = VM_READ | VM_WRITE | VM_STACK;
mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE, vm_flags, NULL);
// 分配栈页面（预分配4页）
pgdir_alloc_page(mm->pgdir, USTACKTOP - PGSIZE, PTE_USER);
pgdir_alloc_page(mm->pgdir, USTACKTOP - 2 * PGSIZE, PTE_USER);
pgdir_alloc_page(mm->pgdir, USTACKTOP - 3 * PGSIZE, PTE_USER);
pgdir_alloc_page(mm->pgdir, USTACKTOP - 4 * PGSIZE, PTE_USER);
```

- 用户栈位于 `USTACKTOP` 以下，向低地址增长。需要为栈区域建立 VMA 并分配物理页。
- `PTE_USER` 宏包含了 `PTE_R | PTE_W | PTE_X | PTE_U | PTE_V`。

**步骤6：设置命令行参数**

在用户栈上构建 `argc` 和 `argv`，栈布局如下（高地址到低地址）：

```
┌─────────────────────┐ 
│   arg[argc-1] 字符串 │
│   ...               │
│   arg[0] 字符串      │
├─────────────────────┤
│   argv[argc] = NULL │
│   argv[argc-1]      │
│   ...               │
│   argv[0]           │
└─────────────────────┘ 
```

```c
// 辅助宏：将用户空间地址转换为内核可访问的地址
#define UADDR_TO_KADDR(mm, uaddr) ({ \
    pte_t *ptep = get_pte((mm)->pgdir, (uaddr), 0); \
    uintptr_t pa = PTE_ADDR(*ptep) | ((uaddr) & 0xFFF); \
    KADDR(pa); \
})

uintptr_t stacktop = USTACKTOP;
uintptr_t argv_addrs[EXEC_MAX_ARG_NUM];

// 复制参数字符串到栈上（使用内核地址写入）
for (int i = argc - 1; i >= 0; i--) {
    size_t arg_len = strlen(kargv[i]) + 1;
    stacktop -= arg_len;
    stacktop = stacktop & ~0x7;  // 8字节对齐
    char *kaddr = (char *)UADDR_TO_KADDR(mm, stacktop);
    strcpy(kaddr, kargv[i]);
    argv_addrs[i] = stacktop;  // 保存用户空间地址
}

// 放置 argv 指针数组
stacktop = stacktop & ~0xF;  // 16字节对齐
stacktop -= sizeof(uintptr_t) * (argc + 1);
uintptr_t *argv_ptr = (uintptr_t *)UADDR_TO_KADDR(mm, stacktop);
uintptr_t argv_user_addr = stacktop;  // 保存用户空间地址
for (int i = 0; i < argc; i++) {
    argv_ptr[i] = argv_addrs[i];
}
argv_ptr[argc] = 0;  // NULL 结尾
```

**步骤7：设置当前进程的 mm 和页目录**

```c
mm_count_inc(mm);
current->mm = mm;
current->pgdir = PADDR(mm->pgdir);
lsatp(current->pgdir);  // 切换页表
```

**步骤8：设置 trapframe**

配置 trapframe 使进程返回用户态时能正确执行：

```c
struct trapframe *tf = current->tf;
memset(tf, 0, sizeof(struct trapframe));

tf->gpr.sp = stacktop;                   // 栈指针
tf->gpr.a0 = argc;                       // 第一个参数
tf->gpr.a1 = argv_user_addr;             // 第二个参数（用户空间地址）
tf->epc = elf->e_entry;                  // 程序入口地址
tf->status = read_csr(sstatus) & ~SSTATUS_SPP;  // 用户态
tf->status |= SSTATUS_SPIE;              // 使能中断
```

### 3. Lab 5 vs. Lab 8

| 方面 | LAB5 | LAB8 |
|------|------|------|
| 程序来源 | 内存中二进制数据 | SFS 文件系统读取磁盘数据 |
| 读取方式 | 直接内存访问 | 通过 `load_icode_read()` 调用文件系统 |
| 文件描述符 | 无 | 需要管理 fd 并在加载后关闭 |
| 参数传递 | 简单的 argc/argv | 完整的用户栈参数构建 |

### 4. 核心函数方法

| 函数/宏 | 功能 |
|---------|------|
| `mm_create()` | 创建内存管理结构 |
| `setup_pgdir()` | 分配并初始化页目录 |
| `load_icode_read()` | 从文件读取数据到内核缓冲区（使用 `file_read`） |
| `mm_map()` | 建立虚拟内存区域 (VMA) |
| `pgdir_alloc_page()` | 分配物理页并建立页表映射 |
| `sysfile_close()` | 关闭文件描述符 |
| `get_pte()` | 获取虚拟地址对应的页表项 |
| `UADDR_TO_KADDR()` | 将用户空间地址转换为内核可访问的地址 |
| `page2kva()` | 将物理页转换为内核虚拟地址 |

### 5. 测试结果

```shell
yname@LAPTOP-R0L8L50O:/mnt/f/Homework/TY_part1/OS/lab8$ make qemu

OpenSBI v0.4 (Jul  2 2019 11:53:53)
   ____                    _____ ____ _____
  / __ \                  / ____|  _ \_   _|
 | |  | |_ __   ___ _ __ | (___ | |_) || |
 | |  | | '_ \ / _ \ '_ \ \___ \|  _ < | |
 | |__| | |_) |  __/ | | |____) | |_) || |_
  \____/| .__/ \___|_| |_|_____/|____/_____|
        | |
        |_|

Platform Name          : QEMU Virt Machine
Platform HART Features : RV64ACDFIMSU
Platform Max HARTs     : 8
Current Hart           : 0
Firmware Base          : 0x80000000
Firmware Size          : 112 KB
Runtime SBI Version    : 0.1

PMP0: 0x0000000080000000-0x000000008001ffff (A)
PMP1: 0x0000000000000000-0xffffffffffffffff (A,R,W,X)
(THU.CST) os is loading ...

Special kernel symbols:
  entry  0xc020004a (virtual)
  etext  0xc020b7f4 (virtual)
  edata  0xc0291060 (virtual)
  end    0xc0296910 (virtual)
Kernel executable memory footprint: 603KB
DTB Init
HartID: 0
DTB Address: 0x82200000
Physical Memory from DTB:
  Base: 0x0000000080000000
  Size: 0x0000000008000000 (128 MB)
  End:  0x0000000087ffffff
DTB init completed
memory management: default_pmm_manager
physcial memory map:
  memory: 0x08000000, [0x80000000, 0x87ffffff].
vapaofset is 18446744070488326144
check_alloc_page() succeeded!
Page table directory switch succeeded!
Kernel stack guardians set succeeded!
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
use SLOB allocator
kmalloc_init() succeeded!
check_vma_struct() succeeded!
check_vmm() succeeded.
sched class: RR_scheduler
Initrd: 0xc0214010 - 0xc021bd0f, size: 0x00007d00
Initrd: 0xc021bd10 - 0xc029100f, size: 0x00075300
sfs: mount: 'simple file system' (107/10/117)
vfs: mount disk0.
++ setup timer interrupts
kernel_execve: pid = 2, name = "sh".
user sh is running!!!
Hello world!!.
I am process 3.
hello pass.
```

---

## 扩展练习 Challenge1：完成基于“UNIX的PIPE机制”的设计方案

### 1. UNIX 管道 (Pipe) 机制原理

UNIX 管道是一种进程间通信 (IPC) 机制，它提供了一个单向的数据流通道。管道本质上是一个内核缓冲区，数据从一端（写端）写入，从另一端（读端）读出。它遵循 FIFO（先进先出）原则。

*   **缓冲区**：管道在内核中维护一个固定大小的缓冲区（通常是环形缓冲）。
*   **阻塞 I/O**：
    *   **读阻塞**：当缓冲区为空时，读取进程会被阻塞，直到有数据写入。
    *   **写阻塞**：当缓冲区已满时，写入进程会被阻塞，直到有数据被读出腾出空间。
*   **生命周期**：管道是匿名的，通常由 `pipe()` 系统调用创建，并通过 `fork()` 传递给子进程，从而实现父子进程或兄弟进程间的通信。当所有引用该管道的文件描述符都被关闭后，管道资源被释放。

### 2. ucore 中的设计方案

在 ucore 中实现管道，可以将其视为一种特殊的文件系统或设备。为了融入现有的 VFS 架构，我们可以定义一种新的 inode 类型，并为其提供特定的操作函数 (`inode_ops`)。

#### 2.1 数据结构设计

我们需要一个结构体来维护管道的状态，包括缓冲区、读写指针和同步原语。这个结构体可以作为 `inode` 的私有数据（类似于 `sfs_inode` 或 `device`）。

```c
#define PIPE_SIZE 4096

/* 管道的内存 inode 信息 */
struct pipe_inode_info {
    char *buffer;               // 数据缓冲区 (由 kmalloc 分配)
    size_t size;                // 缓冲区大小 (PIPE_SIZE)
    size_t head;                // 写入位置 (环形缓冲: write_pos % size)
    size_t tail;                // 读取位置 (环形缓冲: read_pos % size)
    size_t data_len;            // 当前缓冲区内的数据长度
    
    semaphore_t sem;            // 互斥信号量，保护对 buffer 和其他元数据的访问
    wait_queue_t wait_reader;   // 读等待队列：当缓冲区为空时，读者在此等待
    wait_queue_t wait_writer;   // 写等待队列：当缓冲区满时，写者在此等待
    
    int readers;                // 当前打开读端的文件描述符计数
    int writers;                // 当前打开写端的文件描述符计数
    bool is_closed;             // 管道是否已完全关闭
};

/* 扩展 kern/fs/vfs/inode.h 中的 inode 结构 (概念性修改) */
/* 
struct inode {
    union {
        struct device __device_info;
        struct sfs_inode __sfs_inode_info;
        struct pipe_inode_info __pipe_inode_info; // 新增管道信息
    } in_info;
    ...
};
*/
```

#### 2.2 接口设计

我们需要实现一组 `inode_ops`，用于 VFS 层调用。

```c
/* 管道文件的操作函数表 */
static const struct inode_ops pipe_node_ops = {
    .vop_magic          = VOP_MAGIC,
    .vop_open           = pipe_open,
    .vop_close          = pipe_close,
    .vop_read           = pipe_read,
    .vop_write          = pipe_write,
    .vop_fstat          = pipe_fstat,
    // 其他操作如 seek 在管道上通常是无效的
};
```

**核心函数语义：**

1.  **`pipe_read(struct inode *node, struct iobuf *iob)`**:
    *   获取互斥锁 `sem`。
    *   循环检查：如果缓冲区为空 (`data_len == 0`)：
        *   如果写端已关闭 (`writers == 0`)，则返回 0 (EOF)。
        *   否则，释放锁，在 `wait_reader` 上等待，被唤醒后重新获取锁。
    *   从 `buffer` 的 `tail` 位置读取数据到 `iob`，更新 `tail` 和 `data_len`。
    *   唤醒 `wait_writer` (因为有了空闲空间)。
    *   释放锁。

2.  **`pipe_write(struct inode *node, struct iobuf *iob)`**:
    *   获取互斥锁 `sem`。
    *   如果读端已关闭 (`readers == 0`)，发送 `SIGPIPE` 信号（如果实现了信号机制）或返回错误 `E_PIPE`。
    *   循环检查：如果缓冲区已满 (`data_len == size`)：
        *   释放锁，在 `wait_writer` 上等待，被唤醒后重新获取锁。
    *   将数据写入 `buffer` 的 `head` 位置，更新 `head` 和 `data_len`。
    *   唤醒 `wait_reader` (因为有了新数据)。
    *   释放锁。

3.  **`pipe_close(struct inode *node)`**:
    *   获取互斥锁。
    *   根据关闭的文件描述符模式（读或写），递减 `readers` 或 `writers`。
    *   如果 `readers` 变为 0，唤醒所有 `wait_writer` (写者会发现读端关闭并报错)。
    *   如果 `writers` 变为 0，唤醒所有 `wait_reader` (读者会读到 EOF)。
    *   如果 `readers == 0` 且 `writers == 0`，释放 `buffer` 和 `pipe_inode_info` 结构。
    *   释放锁。

4.  **`sys_pipe(int *fd_store)` (系统调用)**:
    *   分配一个新的 `inode`，初始化其 `in_ops` 为 `pipe_node_ops`。
    *   分配并初始化 `pipe_inode_info`。
    *   创建两个 `struct file` 对象：
        *   `file[0]`: 只读模式，指向该 inode。
        *   `file[1]`: 只写模式，指向该 inode。
    *   在当前进程分配两个文件描述符 `fd[0]` 和 `fd[1]`，分别指向上述两个 `file` 对象。
    *   将 `fd[0]` 和 `fd[1]` 返回给用户。

#### 2.3 同步互斥处理

*   **互斥 (Mutual Exclusion)**: 使用 `semaphore_t sem` (初始化为 1) 作为互斥锁。所有对 `buffer`、`head`、`tail`、`data_len` 以及引用计数的操作都必须在持有该锁的临界区内进行，防止多进程/线程并发访问导致数据竞争。
*   **同步 (Synchronization)**:
    *   **读者等待**: 当缓冲区空时，读者无法继续，必须等待。使用 `wait_queue_t wait_reader`。写者写入数据后，调用 `wakeup_queue(&wait_reader)` 唤醒读者。
    *   **写者等待**: 当缓冲区满时，写者无法继续，必须等待。使用 `wait_queue_t wait_writer`。读者读出数据后，调用 `wakeup_queue(&wait_writer)` 唤醒写者。
    *   **死锁避免**: 在进入 `wait_queue` 睡眠之前，必须释放持有的互斥锁 `sem`，否则会造成死锁（持有锁睡眠，导致对方无法获取锁来改变状态唤醒自己）。被唤醒后，必须重新竞争获取锁。这与管程 (Monitor) 的 `Condition Variable` 机制类似。

---

## 扩展练习 Challenge2：完成基于“UNIX的软连接和硬连接机制”的设计方案

### 1. UNIX 软链接与硬链接机制原理

*   **硬链接 (Hard Link)**:
    *   **原理**: 硬链接本质上是文件系统目录中的一个目录项 (Directory Entry)，它指向一个已经存在的 inode。也就是说，多个文件名指向同一个 inode。
    *   **特性**:
        *   所有硬链接地位平等，没有“原始文件”之分。
        *   Inode 中维护一个引用计数 (`nlinks`)。每增加一个硬链接，计数加 1；删除一个硬链接，计数减 1。
        *   只有当 `nlinks` 为 0 且没有进程打开该文件时，文件数据才会被真正释放。
        *   限制：通常不能跨文件系统，不能对目录创建硬链接（防止环路）。

*   **软链接 (Soft Link / Symbolic Link)**:
    *   **原理**: 软链接是一个特殊类型的文件，其数据块中存储的内容是另一个文件的路径字符串。
    *   **特性**:
        *   软链接有自己的 inode 和数据块。
        *   访问软链接时，内核会读取其内容（目标路径），并重定向到该路径。
        *   可以跨文件系统，可以指向目录。
        *   如果目标文件被删除，软链接变成“死链” (Dangling Link)。

### 2. ucore 中的设计方案

为了在 ucore 中实现软硬链接，我们需要在现有的 VFS 和 SFS 架构上进行扩展。主要涉及 `inode_ops` 操作函数表的扩充以及 SFS 文件系统对新文件类型的支持。

#### 2.1 数据结构设计

**1. 扩展文件类型 (SFS Layer)**

在 `kern/fs/sfs/sfs.h` 中，现有的文件类型定义如下：
```c
#define SFS_TYPE_FILE   1
#define SFS_TYPE_DIR    2
```
我们需要增加软链接的类型定义：
```c
#define SFS_TYPE_LINK   3  // 新增：符号链接类型
```

**2. 扩展 VFS 操作接口 (VFS Layer)**

在 `kern/fs/vfs/inode.h` 中，`struct inode_ops` 定义了所有文件系统必须实现的操作。我们需要向其中添加处理链接的函数指针：

```c
struct inode_ops {
    unsigned long vop_magic;
    ...
    // 现有接口
    int (*vop_create)(struct inode *node, const char *name, bool excl, struct inode **node_store);
    int (*vop_lookup)(struct inode *node, char *path, struct inode **node_store);
    
    // 新增接口
    /* 在目录 node 下创建名为 name 的硬链接，指向 target_node */
    int (*vop_link)(struct inode *node, const char *name, struct inode *target_node);
    
    /* 在目录 node 下创建名为 name 的软链接，内容为 path */
    int (*vop_symlink)(struct inode *node, const char *name, const char *path);
    
    /* 读取软链接 node 指向的路径内容到 iob 中 */
    int (*vop_readlink)(struct inode *node, struct iobuf *iob);
};
```

**3. 磁盘索引节点 (SFS Layer)**

`kern/fs/sfs/sfs.h` 中的 `struct sfs_disk_inode` 已经包含 `nlinks` 字段，无需修改结构体定义，只需在实现中正确维护它。
```c
struct sfs_disk_inode {
    ...
    uint16_t nlinks; // 硬链接计数：创建硬链接时+1，删除时-1
    ...
};
```

#### 2.2 接口设计与实现逻辑

我们需要在 SFS 层实现上述新增的 VFS 接口 (`sfs_link`, `sfs_symlink`, `sfs_readlink`)，并修改 VFS 层的路径查找逻辑。

**1. 硬链接实现: `sfs_link`**

该函数对应 `vop_link`。
*   **语义**: 在目录 `dir_node` 中创建一个新目录项 `name`，使其 inode 编号指向 `target_node` 的 inode 编号。
*   **逻辑**:
    1.  检查 `target_node` 类型：通常不允许对目录创建硬链接（避免环路）。
    2.  检查是否跨文件系统：硬链接必须在同一 FS 内（比较 `dir_node->in_fs` 和 `target_node->in_fs`）。
    3.  在 `dir_node` 的数据块中添加一个新的 `sfs_disk_entry`，其 `ino` 为 `target_node` 的 inode 号。
    4.  **关键**: 增加 `target_node` 的引用计数 `nlinks`，并标记 `target_node` 为 dirty 以便写回磁盘。

**2. 软链接实现: `sfs_symlink`**

该函数对应 `vop_symlink`。
*   **语义**: 创建一个新的 inode，类型为 `SFS_TYPE_LINK`，其数据内容为目标路径字符串。
*   **逻辑**:
    1.  调用 `sfs_alloc_inode` 分配一个新的 inode。
    2.  设置新 inode 的 `din->type` 为 `SFS_TYPE_LINK`。
    3.  将目标路径 `path` 写入新 inode 的数据块中（使用 `sfs_write` 逻辑）。
    4.  在父目录 `dir_node` 中添加新 inode 的目录项。

**3. 读取软链接: `sfs_readlink`**

该函数对应 `vop_readlink`。
*   **语义**: 读取符号链接文件存储的路径字符串。
*   **逻辑**:
    1.  检查 `node` 类型是否为 `SFS_TYPE_LINK`。
    2.  利用现有的 `sfs_read` 逻辑，将 inode 数据块中的内容（即路径）读取到 `iobuf` 中。

**4. 路径查找修改: `vfs_lookup` (VFS Layer)**

现有的 `vfs_lookup` (在 `kern/fs/vfs/vfslookup.c`) 只是简单调用 `vop_lookup`。为了支持软链接，需要修改路径解析逻辑（通常在 `vfs_lookup` 或其调用的辅助函数中）：

*   **逻辑扩展**:
    1.  当 `vop_lookup` 返回一个 inode 后，检查其类型。
    2.  如果是 `SFS_TYPE_LINK`：
        *   调用 `vop_readlink` 读取其存储的目标路径。
        *   如果目标路径以 `/` 开头，将当前查找的根重置为 `bootfs` 或 `root`。
        *   如果目标路径是相对路径，从当前目录继续。
        *   **递归限制**: 维护一个计数器（如 `link_count`），如果连续解析软链接超过阈值（如 5 次），返回 `ELOOP` 错误，防止死循环。
    3.  如果是普通文件或目录，继续正常的路径解析。

#### 2.3 同步互斥处理

*   **目录操作互斥**: `sfs_link` 和 `sfs_symlink` 都涉及修改父目录的数据（添加目录项）。必须持有父目录 inode 的信号量 (`sem`)，这在 ucore 现有的 `sfs_lookup` 等操作中已经通过 `lock_sin(sin)` 机制涵盖，需确保新函数也遵守此规则。
*   **链接计数原子性**: 修改 `nlinks` 时（在 `sfs_link` 和 `sfs_unlink` 中），必须持有目标 inode 的锁，防止并发修改导致计数错误。
*   **死锁预防**: 在 `sfs_link(dir, name, target)` 中，可能需要同时持有 `dir` 和 `target` 的锁。应遵循统一的加锁顺序（如按 inode 编号大小，或先目录后文件），或者在 ucore 简化模型中，由于 `target` 只修改元数据且不涉及复杂依赖，可以采用短临界区保护 `nlinks` 更新。

---

## 其他说明：VFS 与 SFS 原理分析

### 1. 文件系统层次结构概述

ucore 的文件系统采用分层设计，从上到下分为四层：

```
┌─────────────────────────────────────────────────────────┐
│                    用户程序层                            │
│              (open, read, write, close)                 │
├─────────────────────────────────────────────────────────┤
│                  系统调用层 (sysfile)                    │
│     sysfile_open, sysfile_read, sysfile_write, ...      │
├─────────────────────────────────────────────────────────┤
│                   文件层 (file)                          │
│       file_open, file_read, file_write, ...             │
│     管理文件描述符、读写位置、打开状态                    │
├─────────────────────────────────────────────────────────┤
│              虚拟文件系统层 (VFS)                        │
│       vfs_open, vfs_read, vfs_lookup, ...               │
│     提供统一的文件系统抽象接口                           │
├─────────────────────────────────────────────────────────┤
│              具体文件系统层 (SFS/设备)                   │
│    sfs_read, sfs_write, sfs_lookup, dev_read, ...       │
│     实现具体的文件系统逻辑                               │
├─────────────────────────────────────────────────────────┤
│                    设备驱动层                            │
│              disk0_read, disk0_write, ...               │
└─────────────────────────────────────────────────────────┘
```

### 2. 虚拟文件系统 (VFS) 原理

#### 2.1 VFS 的作用

VFS（Virtual File System）是一个抽象层，它：

1. **统一接口**：为上层提供统一的文件操作接口（open、read、write 等），屏蔽底层不同文件系统的差异
2. **多文件系统支持**：支持同时挂载多个不同类型的文件系统（如 SFS、设备文件系统等）
3. **路径解析**：处理文件路径，将路径转换为对应的 inode

#### 2.2 核心数据结构

**`struct fs` - 文件系统结构**

```c
struct fs {
    union {
        struct sfs_fs __sfs_info;    // SFS 文件系统特有数据
    } fs_info;
    enum { fs_type_sfs_info } fs_type;  // 文件系统类型
    
    // 文件系统操作函数指针
    int (*fs_sync)(struct fs *fs);              // 同步到磁盘
    struct inode *(*fs_get_root)(struct fs *fs); // 获取根目录 inode
    int (*fs_unmount)(struct fs *fs);           // 卸载文件系统
    void (*fs_cleanup)(struct fs *fs);          // 清理资源
};
```

**`struct inode` - 索引节点结构**

```c
struct inode {
    union {
        struct device __device_info;      // 设备文件
        struct sfs_inode __sfs_inode_info; // SFS 文件
    } in_info;
    enum { inode_type_device_info, inode_type_sfs_inode_info } in_type;
    
    int ref_count;               // 引用计数
    int open_count;              // 打开计数
    struct fs *in_fs;            // 所属文件系统
    const struct inode_ops *in_ops;  // inode 操作函数表
};
```

**`struct inode_ops` - inode 操作接口**

```c
struct inode_ops {
    int (*vop_open)(struct inode *node, uint32_t open_flags);
    int (*vop_close)(struct inode *node);
    int (*vop_read)(struct inode *node, struct iobuf *iob);
    int (*vop_write)(struct inode *node, struct iobuf *iob);
    int (*vop_fstat)(struct inode *node, struct stat *stat);
    int (*vop_lookup)(struct inode *node, char *path, struct inode **node_store);
    int (*vop_create)(struct inode *node, const char *name, bool excl, struct inode **node_store);
    // ... 其他操作
};
```

#### 2.3 VFS 工作流程

以 `open("/disk0/hello.txt", O_RDONLY)` 为例：

```
用户调用 open("/disk0/hello.txt", O_RDONLY)
    ↓
sysfile_open() [kern/fs/sysfile.c]
    ↓
file_open() [kern/fs/file.c]
    ├─ 分配文件描述符 fd
    └─ vfs_open() [kern/fs/vfs/vfsfile.c]
        ↓
        vfs_lookup("/disk0/hello.txt") [kern/fs/vfs/vfslookup.c]
            ├─ 解析路径，找到 "disk0" 设备
            ├─ 获取该文件系统的根 inode
            └─ 调用 vop_lookup 逐级查找 "hello.txt"
                ↓
                sfs_lookup() [kern/fs/sfs/sfs_inode.c]
                    ├─ 在目录中查找文件名
                    └─ 返回文件的 inode
        ↓
        vop_open(inode, open_flags)
            ↓
            sfs_openfile() - 检查打开权限
        ↓
        返回 inode 给 file_open
    ↓
    设置 file 结构的 node、readable、writable 等字段
    ↓
返回文件描述符 fd
```

#### 2.4 VFS 的多态机制

VFS 通过函数指针实现多态，使同一接口能调用不同文件系统的实现：

```c
// VFS 层调用（不关心具体文件系统）
vop_read(node, iob);

// 宏展开后实际调用
node->in_ops->vop_read(node, iob);

// 对于 SFS 文件：调用 sfs_read()
// 对于设备文件：调用 dev_read()
```

### 3. Simple File System (SFS) 原理

#### 3.1 SFS 磁盘布局


| 区域 | 块号 | 功能 |
|------|------|------|
| SuperBlock | 0 | 存储文件系统元信息（魔数、总块数、空闲块数） |
| Root Dir | 1 | 根目录的 inode |
| FreeMap | 2~N | 位图，标记每个块是否被使用 |
| 数据区 | N+1~ | 存储文件内容和 inode |

#### 3.2 核心数据结构

**`struct sfs_super` - 超级块**

```c
struct sfs_super {
    uint32_t magic;        // 魔数 0x2f8dbe2a，用于识别 SFS
    uint32_t blocks;       // 文件系统总块数
    uint32_t unused_blocks;// 空闲块数
    char info[32];         // 文件系统描述信息
};
```

**`struct sfs_disk_inode` - 磁盘 inode**

```c
struct sfs_disk_inode {
    uint32_t size;              // 文件大小（字节）
    uint16_t type;              // 文件类型（普通文件/目录/链接）
    uint16_t nlinks;            // 硬链接数
    uint32_t blocks;            // 占用的块数
    uint32_t direct[12];        // 12 个直接块指针
    uint32_t indirect;          // 间接块指针
};
```

**`struct sfs_inode` - 内存 inode**

```c
struct sfs_inode {
    struct sfs_disk_inode *din; // 指向磁盘 inode 的缓存
    uint32_t ino;               // inode 编号
    bool dirty;                 // 是否被修改
    int reclaim_count;          // 回收计数
    semaphore_t sem;            // 互斥信号量
    list_entry_t inode_link;    // 链入 sfs_fs 的 inode 列表
    list_entry_t hash_link;     // 链入哈希表
};
```

**`struct sfs_fs` - SFS 文件系统实例**

```c
struct sfs_fs {
    struct sfs_super super;     // 超级块缓存
    struct device *dev;         // 底层设备
    struct bitmap *freemap;     // 空闲块位图
    bool super_dirty;           // 超级块是否被修改
    void *sfs_buffer;           // I/O 缓冲区
    semaphore_t fs_sem;         // 文件系统锁
    semaphore_t io_sem;         // I/O 锁
    list_entry_t inode_list;    // 所有加载的 inode 链表
    list_entry_t *hash_list;    // inode 哈希表
};
```

#### 3.3 SFS 文件读取流程

以读取文件 `/hello.txt` 的 100 字节为例：

```
file_read(fd, buf, 100)
    ↓
vop_read(file->node, iob)
    ↓
sfs_read(node, iob) [kern/fs/sfs/sfs_inode.c]
    ↓
sfs_io(node, iob, false) - 加锁保护
    ↓
sfs_io_nolock(sfs, sin, buf, offset, &len, false)
    │
    ├─ 计算起始块号: blkno = offset / SFS_BLKSIZE
    ├─ 计算块内偏移: blkoff = offset % SFS_BLKSIZE
    │
    ├─ 【处理首块】若 blkoff != 0
    │   ├─ sfs_bmap_load_nolock() - 获取物理块号
    │   └─ sfs_rbuf() - 读取块内部分数据
    │
    ├─ 【处理中间完整块】
    │   ├─ sfs_bmap_load_nolock() - 获取物理块号
    │   └─ sfs_rblock() - 读取整块
    │
    └─ 【处理末块】若结束位置不对齐
        ├─ sfs_bmap_load_nolock() - 获取物理块号
        └─ sfs_rbuf() - 读取块内部分数据
```

#### 3.4 块映射机制 (`sfs_bmap_load_nolock`)

SFS 使用两级索引管理文件数据块：

```
逻辑块号 0~11  → 直接索引 direct[0~11]
逻辑块号 12~1035 → 间接索引 indirect → 间接块内的指针
```

```c
// 获取逻辑块号对应的物理块号
static int sfs_bmap_load_nolock(sfs, sin, index, &ino) {
    if (index < SFS_NDIRECT) {
        // 直接索引
        ino = sin->din->direct[index];
    } else {
        // 间接索引：先读取间接块，再从中获取物理块号
        sfs_rbuf(sfs, &ino, sizeof(uint32_t), sin->din->indirect, 
                 (index - SFS_NDIRECT) * sizeof(uint32_t));
    }
}
```

#### 3.5 目录结构

SFS 目录是特殊的文件，其内容由多个 `sfs_disk_entry` 组成：

```c
struct sfs_disk_entry {
    uint32_t ino;                    // 文件的 inode 编号
    char name[SFS_MAX_FNAME_LEN + 1]; // 文件名
};
```

每个目录项占用一个完整的块（4KB），目录查找时逐个读取目录项并比较文件名。

### 4. 文件描述符与进程的关系

#### 4.1 进程的文件管理结构

```c
struct files_struct {
    struct inode *pwd;       // 当前工作目录的 inode
    struct file *fd_array;   // 文件描述符数组
    int files_count;         // 打开的文件数
    semaphore_t files_sem;   // 保护锁
};

struct file {
    enum { FD_NONE, FD_INIT, FD_OPENED, FD_CLOSED } status;
    bool readable;           // 是否可读
    bool writable;           // 是否可写
    int fd;                  // 文件描述符编号
    off_t pos;               // 当前读写位置
    struct inode *node;      // 关联的 inode
    int open_count;          // 打开计数
};
```

#### 4.2 文件描述符的生命周期

```
fork() 时:
  └─ copy_files() - 复制父进程的 files_struct
      └─ dup_files() - 增加每个打开文件的引用计数

exec() 时:
  └─ 保持 files_struct 不变（文件描述符继承）

close(fd) 时:
  └─ file_close(fd)
      ├─ 减少 file->open_count
      └─ 若 open_count == 0，调用 vfs_close()

exit() 时:
  └─ files_closeall() - 关闭所有打开的文件
```

### 5. VFS 与 SFS 的协作关系

```
                    VFS 层
                      │
    ┌─────────────────┼─────────────────┐
    │                 │                 │
    ▼                 ▼                 ▼
┌───────┐       ┌───────────┐      ┌─────────┐
│ SFS   │       │ DeviceFS  │      │ 其他FS  │
│       │       │ (stdin/   │      │         │
│ disk0 │       │  stdout)  │      │         │
└───┬───┘       └─────┬─────┘      └────┬────┘
    │                 │                  │
    ▼                 ▼                  ▼
┌───────┐       ┌───────────┐      ┌─────────┐
│ 块设备 │       │ 字符设备   │      │  ...    │
│ ramdisk│       │ 控制台     │      │         │
└───────┘       └───────────┘      └─────────┘
```

VFS 通过 `inode_ops` 和 `fs` 的函数指针表，将统一的操作接口映射到具体的文件系统实现：

| VFS 接口 | SFS 实现 | 设备文件实现 |
|----------|----------|--------------|
| `vop_read` | `sfs_read` | `dev_read` |
| `vop_write` | `sfs_write` | `dev_write` |
| `vop_lookup` | `sfs_lookup` | `dev_lookup` |
| `vop_create` | `sfs_create` | 不支持 |

