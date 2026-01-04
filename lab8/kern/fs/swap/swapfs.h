#ifndef __KERN_FS_SWAP_SWAPFS_H__
#define __KERN_FS_SWAP_SWAPFS_H__

#include <memlayout.h>

// 从 swap entry 中提取偏移量（高位存储 swap 偏移，低10位是标志位）
#define swap_offset(entry) ((entry) >> 10)

void swapfs_init(void);
int swapfs_read(swap_entry_t entry, struct Page *page);
int swapfs_write(swap_entry_t entry, struct Page *page);

#endif /* !__KERN_FS_SWAP_SWAPFS_H__ */
