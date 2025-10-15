#include <pmm.h>
#include <list.h>
#include <string.h>
#include <slub_pmm.h>
#include <stdio.h>

/* SLUB分配器：标准Linux SLUB的简化版本
* 核心架构：保留标准版的分层管理和对象分配逻辑
* 简化部分：去掉CPU本地缓存、着色等复杂优化
*/

// 预设的缓存大小
const int kmalloc_sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};

struct slub_cache cache_pool[KMALLOC_CACHE_COUNT];
struct slub_page slab_pages[MAX_SLAB_PAGES];

// 计算大小的幂次
static inline int size_to_shift(size_t size) {
    if (size <= 8) return 3;
    if (size <= 16) return 4;
    if (size <= 32) return 5;
    if (size <= 64) return 6;
    if (size <= 128) return 7;
    if (size <= 256) return 8;
    if (size <= 512) return 9;
    if (size <= 1024) return 10;
    if (size <= 2048) return 11;
    return -1; // 超出范围（2048以上直接页面分配）
}

// 计算shift对应的大小
static inline size_t shift_to_size(int shift) {
    return 1 << shift;
}

// 地址转换函数
static inline void *page_address(struct Page *page) {
    return (void*)(page2pa(page) + va_pa_offset);
}

static inline struct Page *virt_to_page(void *addr) {
    uintptr_t pa = (uintptr_t)addr - va_pa_offset;
    return pa2page(pa);
}

// 计算对象在页面内的偏移
static inline void *get_object(struct slub_page *slab, int index) {
    char *page_start = (char*)page_address(slab->data_page);
    return page_start + index * slab->object_size;
}

// 根据大小获取缓存名称
static const char *get_cache_name(int size) {
    switch(size) {
        case 8: return "kmalloc-8";
        case 16: return "kmalloc-16";
        case 32: return "kmalloc-32";
        case 64: return "kmalloc-64";
        case 128: return "kmalloc-128";
        case 256: return "kmalloc-256";
        case 512: return "kmalloc-512";
        case 1024: return "kmalloc-1024";
        case 2048: return "kmalloc-2048";
        default: return "kmalloc-unknown";
    }
}

// 从链表中移除节点
static struct slub_page *remove_from_list(struct slub_page *list, struct slub_page *target) {
    if (list == NULL || target == NULL) return list;

    if (list == target) {
        return list->next;
    }

    struct slub_page *current = list;
    while (current->next != NULL && current->next != target) {
        current = current->next;
    }

    if (current->next == target) {
        current->next = target->next;
    }

    return list;
}

// 分配一个slub_page管理结构
static struct slub_page *alloc_slub_page_struct(void) {
    for (int i = 0; i < MAX_SLAB_PAGES; i++) {
        // 简单检查：如果cache字段为NULL，说明这个结构未被使用
        if (slab_pages[i].cache == NULL) {
            return &slab_pages[i];
        }
    }
    return NULL; // 用完了
}

// 创建新的slab页面
static struct slub_page *alloc_slub_page(struct slub_cache *cache) {
    // 分配slub_page管理结构
    struct slub_page *slab = alloc_slub_page_struct();
    if (slab == NULL) return NULL;

    // 分配物理页面
    struct Page *page = alloc_pages(1);
    if (page == NULL) return NULL;

    // 计算对象大小和每页对象数
    int object_size = shift_to_size(cache - cache_pool + KMALLOC_SHIFT_LOW);
    int objects_per_page = PGSIZE / object_size;

    // 初始化slub_page结构
    slab->data_page = page;
    slab->cache = cache;
    slab->next = NULL;
    slab->object_size = object_size;
    slab->total_objects = objects_per_page;
    slab->used_objects = 0;

    // 初始化空闲位图
    for (int i = 0; i < 8; i++) {
        slab->free_bitmap[i] = 0;
    }
    // 设置前objects_per_page位为1（表示空闲）
    for (int i = 0; i < objects_per_page && i < 256; i++) {
        slab->free_bitmap[i / 32] |= (1U << (i % 32));
    }

    // 加入partial_list
    slab->next = cache->partial_list;
    cache->partial_list = slab;
    cache->total_pages++;
    cache->free_objects += objects_per_page;

    cprintf("SLUB: allocated new slab for cache '%s' (size=%d, objects=%d)\n",
            cache->name, object_size, objects_per_page);
    return slab;
}

// 从slab页面分配对象
static void *alloc_from_slub_page(struct slub_page *slab) {
    // 在位图中查找空闲对象
    for (int i = 0; i < slab->total_objects; i++) {
        int bitmap_idx = i / 32;
        int bit_idx = i % 32;

        if (slab->free_bitmap[bitmap_idx] & (1U << bit_idx)) {
            // 找到空闲对象，清空对应位
            slab->free_bitmap[bitmap_idx] &= ~(1U << bit_idx);
            slab->used_objects++;
            slab->cache->free_objects--;

            // 计算对象地址
            char *page_addr = (char*)page_address(slab->data_page);
            return page_addr + i * slab->object_size;
        }
    }

    return NULL; // 没有空闲对象
}

// 释放对象到slab页面
static void free_to_slub_page(struct slub_page *slab, void *obj) {
    // 计算对象在页面内的偏移
    char *page_addr = (char*)page_address(slab->data_page);
    char *obj_addr = (char*)obj;
    int offset = obj_addr - page_addr;

    // 验证地址对齐
    if (offset < 0 || offset >= slab->total_objects * slab->object_size ||
        offset % slab->object_size != 0) {
        cprintf("SLUB: invalid object address %p for slab\n", obj);
        return;
    }

    // 计算对象索引
    int index = offset / slab->object_size;
    int bitmap_idx = index / 32;
    int bit_idx = index % 32;

    // 检查是否已经是空闲的
    if (slab->free_bitmap[bitmap_idx] & (1U << bit_idx)) {
        cprintf("SLUB: double free detected for object %p\n", obj);
        return;
    }

    // 设置位图中的对应位为空闲
    slab->free_bitmap[bitmap_idx] |= (1U << bit_idx);
    slab->used_objects--;
    slab->cache->free_objects++;
}

// 初始化缓存池
static int init_cache_pool(void) {
    // 初始化所有缓存为未初始化状态
    for (int i = 0; i < KMALLOC_CACHE_COUNT; i++) {
        cache_pool[i].name = NULL;
        cache_pool[i].partial_list = NULL;
        cache_pool[i].full_list = NULL;
        cache_pool[i].total_pages = 0;
        cache_pool[i].free_objects = 0;
        cache_pool[i].initialized = 0;
    }

    // 初始化slub_page数组
    for (int i = 0; i < MAX_SLAB_PAGES; i++) {
        slab_pages[i].cache = NULL;  // 标记为未使用
    }

    cprintf("SLUB: initialized cache pool with %d caches and %d slab pages\n",
            KMALLOC_CACHE_COUNT, MAX_SLAB_PAGES);
    return 0;
}

// 在缓存池中初始化缓存
static void create_cache_in_pool(struct slub_cache *cache, int object_size, const char *name) {
    cache->name = name;
    cache->partial_list = NULL;
    cache->full_list = NULL;
    cache->total_pages = 0;
    cache->free_objects = 0;
    cache->initialized = 1;

    cprintf("SLUB: created cache '%s' (size=%d)\n", name, object_size);
}

// 根据大小获取缓存
static struct slub_cache *get_cache(size_t size) {
    if (size == 0 || size > SLUB_MAX_SIZE) return NULL;

    int shift = size_to_shift(size);
    if (shift < 0) return NULL;

    int index = shift - KMALLOC_SHIFT_LOW;
    int actual_size = shift_to_size(shift);

    struct slub_cache *cache = GET_CACHE(index);
    if (!cache->initialized) {
        // 懒加载创建缓存
        const char *cache_name = get_cache_name(actual_size);
        create_cache_in_pool(cache, actual_size, cache_name);
    }

    return cache;
}

// SLUB初始化
void slub_init(void) {
    // 初始化缓存池
    if (init_cache_pool() != 0) {
        panic("SLUB: failed to initialize cache pool\n");
        return;
    }

    cprintf("SLUB: initialized with %d cache sizes (8-2048 bytes)\n", KMALLOC_CACHE_COUNT);
}

// 主要的kmalloc接口
void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    // 如果大于SLUB_MAX_SIZE，直接使用页面分配器
    if (size > SLUB_MAX_SIZE) {
        size_t pages = (size + PGSIZE - 1) / PGSIZE;
        struct Page *page = alloc_pages(pages);
        if (page == NULL) return NULL;

        cprintf("SLUB: large allocation %d bytes -> %d pages\n", (int)size, (int)pages);
        return page_address(page);
    }

    // 获取对应的缓存
    struct slub_cache *cache = get_cache(size);
    if (cache == NULL) return NULL;

    // 先从partial_list中寻找空闲对象
    struct slub_page *slab = cache->partial_list;
    if (slab != NULL) {
        void *obj = alloc_from_slub_page(slab);
        if (obj == NULL) {
            cprintf("SLUB: warning - no free object found in partial_list\n");
            return NULL;
        }

        // 检查是否需要移动到full_list
        if (slab->used_objects == slab->total_objects) {
            // 从partial_list中移除
            cache->partial_list = remove_from_list(cache->partial_list, slab);
            // 加入full_list
            slab->next = cache->full_list;
            cache->full_list = slab;
        }

        int actual_size = shift_to_size(cache - cache_pool + KMALLOC_SHIFT_LOW);
        cprintf("SLUB: allocated %d bytes from cache '%s' (actual size=%d)\n",
                (int)size, cache->name, actual_size);
        return obj;
    }

    // 没有空闲对象，分配新的slab页面
    slab = alloc_slub_page(cache);
    if (slab == NULL) return NULL;

    void *obj = alloc_from_slub_page(slab);
    int actual_size = shift_to_size(cache - cache_pool + KMALLOC_SHIFT_LOW);
    cprintf("SLUB: allocated %d bytes from new slab (cache '%s', actual size=%d)\n",
            (int)size, cache->name, actual_size);
    return obj;
}

// 主要的kfree接口
void kfree(void *ptr, size_t size) {
    if (ptr == NULL) return;

    // 大内存直接释放页面
    if (size > SLUB_MAX_SIZE) {
        struct Page *page = virt_to_page(ptr);
        size_t pages = (size + PGSIZE - 1) / PGSIZE;
        free_pages(page, pages);
        cprintf("SLUB: freed large allocation %d bytes (%d pages)\n", (int)size, (int)pages);
        return;
    }

    // 找到对应的缓存
    struct slub_cache *cache = get_cache(size);
    if (cache == NULL) return;

    int actual_size = shift_to_size(cache - cache_pool + KMALLOC_SHIFT_LOW);

    // 在partial_list中查找对象所在的页面
    struct slub_page *slab = cache->partial_list;
    while (slab != NULL) {
        char *page_start = (char*)page_address(slab->data_page);
        char *page_end = page_start + PGSIZE;

        if ((char*)ptr >= page_start && (char*)ptr < page_end) {
            free_to_slub_page(slab, ptr);

            // 检查页面是否完全空闲
            if (slab->used_objects == 0) {
                // 从partial_list中移除
                cache->partial_list = remove_from_list(cache->partial_list, slab);
                cache->total_pages--;

                // 释放物理页面
                free_pages(slab->data_page, 1);

                // 释放slub_page管理结构
                slab->cache = NULL;

                cprintf("SLUB: freed %d bytes to cache '%s' (actual size=%d), released empty page\n",
                        (int)size, cache->name, actual_size);
            } else {
                cprintf("SLUB: freed %d bytes to cache '%s' (actual size=%d)\n",
                        (int)size, cache->name, actual_size);
            }
            return;
        }
        slab = slab->next;
    }

    // 在full_list中查找对象所在的页面
    slab = cache->full_list;
    while (slab != NULL) {
        char *page_start = (char*)page_address(slab->data_page);
        char *page_end = page_start + PGSIZE;

        if ((char*)ptr >= page_start && (char*)ptr < page_end) {
            free_to_slub_page(slab, ptr);

            // 从full_list移动到partial_list
            cache->full_list = remove_from_list(cache->full_list, slab);
            slab->next = cache->partial_list;
            cache->partial_list = slab;

            cprintf("SLUB: freed %d bytes to cache '%s' (actual size=%d), moved from full to partial\n",
                    (int)size, cache->name, actual_size);
            return;
        }
        slab = slab->next;
    }

    cprintf("SLUB: warning - could not find slab for object %p\n", ptr);
}

/* ==================== 测试函数 ==================== */
// 基础检查函数
static void slub_basic_check(void) {
    void *obj0, *obj1, *obj2;
    obj0 = obj1 = obj2 = NULL;

    // 分配三个小对象
    assert((obj0 = kmalloc(64)) != NULL);
    assert((obj1 = kmalloc(64)) != NULL);
    assert((obj2 = kmalloc(64)) != NULL);

    // 验证三个对象地址不同
    assert(obj0 != obj1 && obj0 != obj2 && obj1 != obj2);

    // 验证地址对齐
    assert((uintptr_t)obj0 % 8 == 0);
    assert((uintptr_t)obj1 % 8 == 0);
    assert((uintptr_t)obj2 % 8 == 0);

    cprintf("slub_basic_check: allocated 3 objects at %lx, %lx, %lx\n",
            (uintptr_t)obj0, (uintptr_t)obj1, (uintptr_t)obj2);

    // 释放所有对象
    kfree(obj0, 64);
    kfree(obj1, 64);
    kfree(obj2, 64);

    // 重新分配，应该能够复用刚释放的内存
    assert((obj0 = kmalloc(64)) != NULL);
    assert((obj1 = kmalloc(64)) != NULL);
    assert((obj2 = kmalloc(64)) != NULL);

    // 验证三个对象地址不同
    assert(obj0 != obj1 && obj0 != obj2 && obj1 != obj2);

    // 验证地址对齐
    assert((uintptr_t)obj0 % 8 == 0);
    assert((uintptr_t)obj1 % 8 == 0);
    assert((uintptr_t)obj2 % 8 == 0);

    cprintf("slub_basic_check: allocated 3 objects at %lx, %lx, %lx\n",
            (uintptr_t)obj0, (uintptr_t)obj1, (uintptr_t)obj2);

    kfree(obj0, 64);
    kfree(obj1, 64);
    kfree(obj2, 64);

    cprintf("slub_basic_check passed!\n");
}

// 页面管理检查函数 - 测试2048字节对象的页面级管理
static void slub_page_check(void) {
    // 对于2048字节对象，每页只能放2个对象

    // 分配两个2048字节对象，应该填满一页
    void *obj0 = kmalloc(2048);
    void *obj1 = kmalloc(2048);
    assert(obj0 != NULL && obj1 != NULL);

    cprintf("slub_page_check: allocated 2 objects (should fill one page): %lx, %lx\n",
            (uintptr_t)obj0, (uintptr_t)obj1);

    // 验证它们在同一页内
    assert(((uintptr_t)obj0 / PGSIZE) == ((uintptr_t)obj1 / PGSIZE));

    // 再分配一个2048字节对象，应该在新页面
    void *obj2 = kmalloc(2048);
    assert(obj2 != NULL);
    cprintf("slub_page_check: allocated 3rd object (should be in new page): %lx\n",
            (uintptr_t)obj2);

    // 验证第三个对象在不同页面
    assert(((uintptr_t)obj0 / PGSIZE) != ((uintptr_t)obj2 / PGSIZE));

    // 按正确顺序释放：先释放第三个对象（独占一页）
    kfree(obj2, 2048);
    cprintf("slub_page_check: freed 3rd object (should free entire page)\n");

    // 再释放前两个对象
    kfree(obj0, 2048);
    kfree(obj1, 2048);
    cprintf("slub_page_check: freed 1st and 2nd objects\n");

    cprintf("slub_page_check passed!\n");
}

// 大内存分配检查
static void slub_large_check(void) {
    // 测试超过SLUB_MAX_SIZE的大内存分配
    void *large_obj = kmalloc(4196);
    assert(large_obj != NULL);

    // 验证页面对齐
    assert((uintptr_t)large_obj % PGSIZE == 0);

    cprintf("slub_large_check: allocated 4196 bytes at %lx (page aligned)\n",
            (uintptr_t)large_obj);

    kfree(large_obj, 4196);
    cprintf("slub_large_check: freed large allocation\n");

    cprintf("slub_large_check passed!\n");
}

// 混合场景检查
static void slub_mixed_check(void) {
    // 分配不同大小的对象
    void *obj_small = kmalloc(100);   // 应该使用kmalloc-128缓存
    void *obj_medium = kmalloc(500);  // 应该使用kmalloc-512缓存
    void *obj_large = kmalloc(2048);  // 应该使用kmalloc-2048缓存

    assert(obj_small != NULL && obj_medium != NULL && obj_large != NULL);

    cprintf("slub_mixed_check: allocated mixed sizes: 100B@%lx, 500B@%lx, 2048B@%lx\n",
            (uintptr_t)obj_small, (uintptr_t)obj_medium, (uintptr_t)obj_large);

    // 验证地址对齐
    assert((uintptr_t)obj_small % 8 == 0);
    assert((uintptr_t)obj_medium % 8 == 0);
    assert((uintptr_t)obj_large % 8 == 0);

    // 释放顺序应该正确
    kfree(obj_small, 100);
    kfree(obj_medium, 500);
    kfree(obj_large, 2048);

    cprintf("slub_mixed_check: freed all mixed allocations\n");
    cprintf("slub_mixed_check passed!\n");
}

// 主测试函数
void slub_check(void) {
    cprintf("\n=== SLUB Comprehensive Test Start ===\n");

    // 1. 基础功能检查
    cprintf("\n--- Basic Check ---\n");
    slub_basic_check();

    // 2. 页面管理检查
    cprintf("\n--- Page Management Check ---\n");
    slub_page_check();

    // 3. 大内存分配检查
    cprintf("\n--- Large Allocation Check ---\n");
    slub_large_check();

    // 4. 混合场景检查
    cprintf("\n--- Mixed Scenario Check ---\n");
    slub_mixed_check();

    cprintf("\n=== SLUB All Tests Passed ===\n");
}