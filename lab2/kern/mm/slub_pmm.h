#ifndef __KERN_MM_SLUB_PMM_H__
#define  __KERN_MM_SLUB_PMM_H__

#include <pmm.h>

#define MAX_SLAB_PAGES 1000             // 最大slab页面数
#define SLUB_MAX_SIZE 2048              // 4096字节直接分配页面，无需SLUB
#define KMALLOC_SHIFT_LOW 3
#define KMALLOC_SHIFT_HIGH 11
#define KMALLOC_CACHE_COUNT (KMALLOC_SHIFT_HIGH - KMALLOC_SHIFT_LOW + 1)
#define GET_CACHE(index) (cache_pool + (index))

// 页面管理结构
struct slub_page {
    struct Page *data_page;           // 指向实际数据页面
    struct slub_cache *cache;         // 属于哪个缓存
    struct slub_page *next;           // 链表下一个节点（在partial或full链表中）
    int object_size;                  // 对象大小
    int total_objects;                // 该页面的总对象数
    int used_objects;                 // 已使用的对象数
    uint32_t free_bitmap[8];          // 空闲位图（32*8=256位，足够管理小对象）
};

// SLUB缓存结构
struct slub_cache {
    const char *name;                  // 缓存名称
    struct slub_page *partial_list;    // 还有空位的slub_page链表
    struct slub_page *full_list;       // 已满的slub_page链表
    int total_pages;                   // 总页面数
    int free_objects;                  // 总空闲对象数
    bool initialized;                  // 是否已初始化
};

extern struct slub_cache cache_pool[KMALLOC_CACHE_COUNT];
extern struct slub_page slab_pages[MAX_SLAB_PAGES];
extern const int kmalloc_sizes[];
extern uint64_t va_pa_offset;

void slub_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr, size_t size);
void slub_check(void);

#endif /* ! __KERN_MM_SLUB_PMM_H__ */

