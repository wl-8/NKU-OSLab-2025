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


## 练习1：完成读文件操作的实现



---

## 练习2：完成基于文件系统的执行程序机制的实现



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
