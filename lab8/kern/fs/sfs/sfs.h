#ifndef __KERN_FS_SFS_SFS_H__
#define __KERN_FS_SFS_SFS_H__

#include <defs.h>
#include <mmu.h>
#include <list.h>
#include <sem.h>
#include <unistd.h>

/*
 * Simple FS (SFS) 简单文件系统定义
 * 
 * 本文件定义了 SFS 的磁盘格式和内存数据结构，
 * 也被 mksfs 等工具用于创建 SFS 卷。
 * 
 * SFS 磁盘布局:
 * +-------+-------+----------+----------+----------+-----+
 * | Super | Root  | Freemap  | Freemap  |  Data    | ... |
 * | Block | Inode | Block 0  | Block 1  | Blocks   |     |
 * +-------+-------+----------+----------+----------+-----+
 * Block:  0       1          2          3          4     ...
 */

/* ==================== 基本常量定义 ==================== */

#define SFS_MAGIC                                   0x2f8dbe2a              /* SFS 魔数，用于标识文件系统类型 */
#define SFS_BLKSIZE                                 PGSIZE                  /* 块大小，等于页大小(4KB) */
#define SFS_NDIRECT                                 12                      /* inode 中直接块指针的数量 */
#define SFS_MAX_INFO_LEN                            31                      /* 文件系统信息字符串最大长度 */
#define SFS_MAX_FNAME_LEN                           FS_MAX_FNAME_LEN        /* 文件名最大长度 */
#define SFS_MAX_FILE_SIZE                           (1024UL * 1024 * 128)   /* 最大文件大小 (128MB) */
#define SFS_BLKN_SUPER                              0                       /* 超级块所在的块号 */
#define SFS_BLKN_ROOT                               1                       /* 根目录 inode 所在的块号 */
#define SFS_BLKN_FREEMAP                            2                       /* 空闲块位图的起始块号 */

/* 一个块中包含的位数 (块大小 * 8) */
#define SFS_BLKBITS                                 (SFS_BLKSIZE * CHAR_BIT)

/* 一个块中可容纳的 uint32_t 条目数量 (用于间接块索引) */
#define SFS_BLK_NENTRY                              (SFS_BLKSIZE / sizeof(uint32_t))

/* ==================== 文件类型定义 ==================== */

#define SFS_TYPE_INVAL                              0       /* 无效类型，不应出现在磁盘上 */
#define SFS_TYPE_FILE                               1       /* 普通文件 */
#define SFS_TYPE_DIR                                2       /* 目录 */
#define SFS_TYPE_LINK                               3       /* 符号链接 */

/* ==================== 磁盘数据结构 ==================== */

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

/* 目录项中文件名字段的大小 */
#define sfs_dentry_size                             \
    sizeof(((struct sfs_disk_entry *)0)->name)

/* ==================== 内存数据结构 ==================== */

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

/* ==================== 哈希表相关定义 ==================== */

#define SFS_HLIST_SHIFT                             10                      /* 哈希表大小的位移量 */
#define SFS_HLIST_SIZE                              (1 << SFS_HLIST_SHIFT)  /* 哈希表大小：1024 个桶 */
#define sin_hashfn(x)                               (hash32(x, SFS_HLIST_SHIFT)) /* 计算 ino 的哈希值 */

/* ==================== 空闲块位图相关宏 ==================== */

/* 空闲块位图所需的总位数（向上取整到 SFS_BLKBITS 的倍数） */
#define sfs_freemap_bits(super)                     ROUNDUP((super)->blocks, SFS_BLKBITS)

/* 空闲块位图占用的块数 */
#define sfs_freemap_blocks(super)                   ROUNDUP_DIV((super)->blocks, SFS_BLKBITS)

/* ==================== 函数声明 ==================== */

struct fs;
struct inode;

/* 初始化 SFS 模块 */
void sfs_init(void);

/* 挂载指定设备上的 SFS 文件系统 */
int sfs_mount(const char *devname);

/* 文件系统级别的锁操作（保护元数据） */
void lock_sfs_fs(struct sfs_fs *sfs);
void unlock_sfs_fs(struct sfs_fs *sfs);

/* I/O 级别的锁操作（保护块设备访问） */
void lock_sfs_io(struct sfs_fs *sfs);
void unlock_sfs_io(struct sfs_fs *sfs);

/* 块级别读写函数 */
int sfs_rblock(struct sfs_fs *sfs, void *buf, uint32_t blkno, uint32_t nblks);  /* 读取连续块 */
int sfs_wblock(struct sfs_fs *sfs, void *buf, uint32_t blkno, uint32_t nblks);  /* 写入连续块 */

/* 缓冲区级别读写函数（支持块内偏移和任意长度） */
int sfs_rbuf(struct sfs_fs *sfs, void *buf, size_t len, uint32_t blkno, off_t offset);  /* 读取部分块 */
int sfs_wbuf(struct sfs_fs *sfs, void *buf, size_t len, uint32_t blkno, off_t offset);  /* 写入部分块 */

/* 同步操作（将内存数据写回磁盘） */
int sfs_sync_super(struct sfs_fs *sfs);     /* 同步超级块 */
int sfs_sync_freemap(struct sfs_fs *sfs);   /* 同步空闲块位图 */

/* 清零指定块 */
int sfs_clear_block(struct sfs_fs *sfs, uint32_t blkno, uint32_t nblks);

/* 加载 inode：根据 ino 从磁盘读取 inode 并缓存到内存 */
int sfs_load_inode(struct sfs_fs *sfs, struct inode **node_store, uint32_t ino);

#endif /* !__KERN_FS_SFS_SFS_H__ */
