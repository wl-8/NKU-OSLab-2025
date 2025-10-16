# lab2：物理内存和页表

## 扩展练习Challenge：任意大小的内存单元slub分配算法

### 4.1 SLUB思想核心与实现差异

SLUB（Simplified Locked Unqueued Buddy）是Linux内核中的一种内存分配器，其核心思想是**用最简单的数据结构实现高效的内存分配**。我的任意大小的内存单元分配算法在缓存管理层面与标准SLUB基本一致，但在对象管理方式上采用了不同的技术路线。

#### 4.1.1 相同的两层架构设计

我的SLUB分配器和标准SLUB一样，采用了两层架构设计：

#### 第一层：页面级内存分配（大内存处理）
- **适用场景**：当申请内存大小超过4KB（`SLUB_MAX_SIZE = 2048`）时
- **实现方式**：直接调用页面分配器（使用Best-Fit算法）
- **优势**：避免大内存分配的开销，直接使用底层页面管理

```c
// 大内存分配逻辑
if (size > SLUB_MAX_SIZE) {
    size_t pages = (size + PGSIZE - 1) / PGSIZE;
    struct Page *page = alloc_pages(pages);
    return page_address(page);
}
```

#### 第二层：SLUB小对象分配器（小内存处理）
- **适用场景**：8-2048字节的小内存分配
- **预设大小**：8, 16, 32, 64, 128, 256, 512, 1024, 2048共9个级别
- **分配策略**：向上取整到最近的预设大小，减少碎片

**架构优势**：
- 大小内存分别处理，避免小内存分配影响大内存性能
- 预设大小分类，简化管理复杂度
- 减少内存碎片，提高分配效率

#### 4.1.2 不同的对象管理方式

#### 标准SLUB vs 我的实现

**标准SLUB的对象管理**：
- **内嵌指针链表**：在每个空闲对象内部存储下一个空闲对象的地址
- **管理信息混用**：空闲对象的前几个字节用于存储管理信息
- **O(1)分配性能**：直接取链表头即可获得空闲对象

**我的实现**：
- **独立管理结构**：每个页面有一个独立的`slub_page`管理结构体
- **位图管理**：使用256位位图记录页面内对象的分配状态
- **数据与管理分离**：数据页面纯存储用户数据，管理信息集中在`slub_page`中，而`slub_page`存储在内核空间

#### 两种方案的优缺点对比

| 特性 | 标准SLUB（内嵌指针） | 我的实现（位图+管理结构） |
|------|-------------------|---------------------------|
| **查找性能** | O(1) - 直接取链表头；直接插入链表头 | O(n) - 线性扫描位图查找空闲对象；线性扫描页面链表定位页面 |
| **内存开销** | 每个空闲对象8字节指针 | 每页面32字节位图+管理结构 |
| **数据纯净度** | 空闲对象混有管理信息 | 数据页面完全纯净 |
| **管理复杂度** | 指针操作，较复杂 | 位图操作，直观简单 |

#### 我的实现的优势

1. **页面分配规整**：每个数据页面都是纯的4KB用户数据区域，不存在管理信息污染
2. **管理信息集中**：所有管理信息都在`slub_page`结构中，便于调试和统计
3. **内存利用率有时较高**：当小对象大量分配时，内存利用率较高（一个`slub_page`结构体大小72字节将小于256个8字节指针）；且`slub_page`结构体完全存储在内核空间，不占用用户数据空间
4. **实现简单可靠**：位图操作直观，不易出错，实现相对容易

#### 我的实现的劣势

1. **查找性能较低**：申请内存时需要线性扫描256位位图来找到空闲对象；释放时也需定位对象所在页面，时间复杂度为O(n)
2. **不适合高频分配**：在内存分配密集的场景下性能可能不如标准SLUB
3. **内存开销可能较大**：固定分配1000个`slub_page`结构体，占用32KB内存
4. **可扩展性有限**：`slub_page`结构体数量固定，无法动态扩展

尽管存在以上差异，我的实现依然保留了SLUB的核心思想：**简化管理结构，实现两级架构的任意大小内存分配**。

### 4.2 实现细节与代码说明

#### 4.2.1 数据结构设计

#### slub_cache结构体 - 缓存管理器

```c
struct slub_cache {
    const char *name;                  // 缓存名称（如"kmalloc-64"）
    struct slub_page *partial_list;    // 部分空闲的slab页面链表
    struct slub_page *full_list;       // 完全占用的slab页面链表
    int total_pages;                   // 该缓存的总页面数
    int free_objects;                  // 该缓存的总空闲对象数
    bool initialized;                  // 初始化标志
};
```

**设计理念**：
- **9个缓存实例**：系统中共有9个slub_cache实例，分别对应8、16、32、64、128、256、512、1024、2048字节
- **双链表管理**：`partial_list`管理有空闲空间的页面，`full_list`管理完全占用的页面
- **状态统计**：记录页面对象使用情况，便于内存管理和调试

#### slub_page结构体 - 页面管理器

```c
struct slub_page {
    struct Page *data_page;           // 指向实际的物理数据页面
    struct slub_cache *cache;         // 指向所属的缓存
    struct slub_page *next;           // 链表指针（用于partial或full链表）
    int object_size;                  // 对象大小（如64、128等）
    int total_objects;                // 该页面可容纳的总对象数
    int used_objects;                 // 已使用的对象数
    uint32_t free_bitmap[8];          // 空闲位图（256位，最多管理256个小对象）
};
```

**设计亮点**：
- **位图管理**：使用256位位图高效管理页面内对象分配状态，每个位对应一个对象
- **链表连接**：通过next指针将页面组织成链表，便于快速遍历和管理
- **归属明确**：每个slub_page明确知道自己属于哪个缓存，便于释放时正确处理

#### 4.2.2 内存分配和释放的实现

#### slub_init() - 初始化函数

```c
void slub_init(void) {
    // 初始化缓存池
    if (init_cache_pool() != 0) {
        panic("SLUB: failed to initialize cache pool\n");
        return;
    }
    cprintf("SLUB: initialized with %d cache sizes (8-2048 bytes)\n", KMALLOC_CACHE_COUNT);
}
```

其中，`init_cache_pool()`函数的实现如下：

```c
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
```

**初始化细节**：
- 预分配9个`slub_cache`结构体，存储在`cache_pool`数组中
- 预分配1000个`slub_page`结构体，存储在`slab_pages`数组中
- 对于`slub_cache`结构体，我将`initialized`字段设置为`0`并初始化其它字段为空或零值，以确保缓存池在使用前处于干净状态
- 对于`slub_page`数组，我将每个页面的`cache`字段设置为`NULL`，表示这些页面当前未被任何缓存使用

#### kmalloc() - 内存分配接口

**第一阶段：大小判断与路由**
```c
    // 如果大于SLUB_MAX_SIZE，直接使用页面分配器
    if (size > SLUB_MAX_SIZE) {
        size_t pages = (size + PGSIZE - 1) / PGSIZE;
        struct Page *page = alloc_pages(pages);
        if (page == NULL) return NULL;
        return page_address(page);
    }
```

**大内存处理逻辑**：
- 计算所需页面数：`(size + PGSIZE - 1) / PGSIZE`实现向上取整
- 直接调用Best-Fit页面分配器
- 返回页面虚拟地址，绕过SLUB的小对象管理

**第二阶段：缓存获取与对象分配**
```c
    // 获取对应的缓存
    struct slub_cache *cache = get_cache(size);
    if (cache == NULL) return NULL;
```

其中，`get_cache(size)`函数的实现如下：

```c
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
```

**缓存获取机制**：
- 通过`size_to_shift()`函数将请求大小转换为对应的2的幂次
- 计算缓存索引：`shift - KMALLOC_SHIFT_LOW`
- 懒加载：如果缓存未初始化，调用`create_cache_in_pool()`创建

**第三阶段：从partial_list分配**
```c
    // 先从partial_list中寻找空闲对象
    struct slub_page *slab = cache->partial_list;
    if (slab != NULL) {
        void *obj = alloc_from_slub_page(slab);
        if (obj == NULL) {
            cprintf("SLUB: warning - no free object found in partial_list\n");
            return NULL;
        }
```

**partial_list分配逻辑**：
- 优先从现有有空间的页面分配，避免页面分配开销
- 调用`alloc_from_slub_page()`在页面内通过位图查找空闲对象
- **位图查找算法详解**：
  ```c
  // 在位图中查找空闲对象 - O(n)时间复杂度
  for (int i = 0; i < slab->total_objects; i++) {
      int bitmap_idx = i / 32;     // 计算位图数组索引
      int bit_idx = i % 32;        // 计算位内偏移

      // 检查该位是否为1（表示对象空闲）
      if (slab->free_bitmap[bitmap_idx] & (1U << bit_idx)) {
          // 找到空闲对象，清除对应位（标记为已使用）
          slab->free_bitmap[bitmap_idx] &= ~(1U << bit_idx);
          slab->used_objects++;

          // 计算对象实际地址并返回
          char *page_addr = (char*)page_address(slab->data_page);
          return page_addr + i * slab->object_size;
      }
  }
  ```

**链表状态管理**：
```c
        // 检查是否需要移动到full_list
        if (slab->used_objects == slab->total_objects) {
            // 从partial_list中移除
            cache->partial_list = remove_from_list(cache->partial_list, slab);
            // 加入full_list
            slab->next = cache->full_list;
            cache->full_list = slab;
        }
```

- 检测页面是否完全占用：`slab->used_objects == slab->total_objects`
- 完全占用的页面从partial_list移到full_list
- 优化后续分配效率：避免遍历满的页面

**第四阶段：新页面分配**
```c
    // 没有空闲对象，分配新的slab页面
    slab = alloc_slub_page(cache);
    if (slab == NULL) return NULL;
    void *obj = alloc_from_slub_page(slab);
```

**新页面分配流程**：
- 调用`alloc_slub_page()`分配新的物理页面和管理结构
- 计算页面内对象数量：`PGSIZE / object_size`
- 初始化256位位图，前`total_objects`位置1（表示空闲）
- 将新页面加入partial_list

#### kfree() - 内存释放接口

**第一阶段：大小判断与路由**
```c
void kfree(void *ptr, size_t size) {
    if (ptr == NULL) return;

    // 大内存直接释放页面
    if (size > SLUB_MAX_SIZE) {
        struct Page *page = virt_to_page(ptr);
        size_t pages = (size + PGSIZE - 1) / PGSIZE;
        free_pages(page, pages);
        return;
    }
```

**大内存释放逻辑**：
- 虚拟地址转物理页面：`virt_to_page(ptr)`
- 计算页面数量并调用页面释放器
- 绕过SLUB小对象管理，直接释放

**第二阶段：缓存获取与页面定位**
```c
    // 找到对应的缓存
    struct slub_cache *cache = get_cache(size);
    if (cache == NULL) return;
```

**第三阶段：在partial_list中查找并释放**
```c
    // 在partial_list中查找对象所在的页面
    struct slub_page *slab = cache->partial_list;
    while (slab != NULL) {
        char *page_start = (char*)page_address(slab->data_page);
        char *page_end = page_start + PGSIZE;

        if ((char*)ptr >= page_start && (char*)ptr < page_end) {
            free_to_slub_page(slab, ptr);
```

**页面定位算法**：
- 遍历partial_list中的每个slab_page
- 比较释放地址与页面地址范围
- 找到对应页面后调用`free_to_slub_page()`

**对象释放逻辑详解**：
```c
            // 计算对象在页面内的索引
            int index = offset / slab->object_size;
            int bitmap_idx = index / 32;    // 位图数组索引
            int bit_idx = index % 32;       // 位内偏移

            // 检查是否已经是空闲的（防止重复释放）
            if (slab->free_bitmap[bitmap_idx] & (1U << bit_idx)) {
                cprintf("SLUB: double free detected for object %p\n", obj);
                return;
            }

            // 设置位图中的对应位为空闲（从0变为1）
            slab->free_bitmap[bitmap_idx] |= (1U << bit_idx);
            slab->used_objects--;
            slab->cache->free_objects++;

            // 检查页面是否完全空闲
            if (slab->used_objects == 0) {
                // 从partial_list中移除
                cache->partial_list = remove_from_list(cache->partial_list, slab);
                cache->total_pages--;

                // 释放物理页面
                free_pages(slab->data_page, 1);

                // 释放slub_page管理结构
                slab->cache = NULL;
```

- 检查页面是否完全空闲：`slab->used_objects == 0`
- 完全空闲的页面立即释放，避免内存浪费
- 清除slub_page管理结构，标记为未使用

**第四阶段：在full_list中查找并释放**
```c
    // 在full_list中查找对象所在的页面
    slab = cache->full_list;
    while (slab != NULL) {
        if ((char*)ptr >= page_start && (char*)ptr < page_end) {
            free_to_slub_page(slab, ptr);

            // 从full_list移动到partial_list
            cache->full_list = remove_from_list(cache->full_list, slab);
            slab->next = cache->partial_list;
            cache->partial_list = slab;
```

**链表状态转换**：
- 释放对象后页面不再是full状态
- 从full_list移到partial_list，便于后续分配使用
- 优化内存利用率，避免满页面浪费

### 4.3 测试设计与验证

#### 4.3.1 测试用例设计

#### slub_basic_check()
- 测试基础分配和释放功能
- 验证地址唯一性和对齐
- 测试内存复用

#### slub_page_check()
- 测试 2048 字节对象的页面管理
- 验证页面填充和新页面分配
- 测试页面释放机制

#### slub_large_check()
- 测试大内存分配（>2048字节）
- 验证页面对齐
- 测试直接页面分配机制

#### slub_mixed_check()
- 测试不同大小对象的混合分配
- 验证缓存选择机制
- 测试复杂释放场景

#### 4.3.2 测试结果与分析

```bash
=== SLUB Allocator Test ===
SLUB: initialized cache pool with 9 caches and 1000 slab pages
SLUB: initialized with 9 cache sizes (8-2048 bytes)

=== SLUB Comprehensive Test Start ===

--- Basic Check ---
SLUB: created cache 'kmalloc-64' (size=64)
SLUB: allocated new slab for cache 'kmalloc-64' (size=64, objects=64)
SLUB: allocated 64 bytes from new slab (cache 'kmalloc-64', actual size=64)
SLUB: allocated 64 bytes from cache 'kmalloc-64' (actual size=64)
SLUB: allocated 64 bytes from cache 'kmalloc-64' (actual size=64)
slub_basic_check: allocated 3 objects at ffffffffc0359000, ffffffffc0359040, ffffffffc0359080
SLUB: freed 64 bytes to cache 'kmalloc-64' (actual size=64)
SLUB: freed 64 bytes to cache 'kmalloc-64' (actual size=64)
SLUB: freed 64 bytes to cache 'kmalloc-64' (actual size=64), released empty page
SLUB: allocated new slab for cache 'kmalloc-64' (size=64, objects=64)
SLUB: allocated 64 bytes from new slab (cache 'kmalloc-64', actual size=64)
SLUB: allocated 64 bytes from cache 'kmalloc-64' (actual size=64)
SLUB: allocated 64 bytes from cache 'kmalloc-64' (actual size=64)
slub_basic_check: allocated 3 objects at ffffffffc0359000, ffffffffc0359040, ffffffffc0359080
SLUB: freed 64 bytes to cache 'kmalloc-64' (actual size=64)
SLUB: freed 64 bytes to cache 'kmalloc-64' (actual size=64)
SLUB: freed 64 bytes to cache 'kmalloc-64' (actual size=64), released empty page
slub_basic_check passed!

--- Page Management Check ---
SLUB: created cache 'kmalloc-2048' (size=2048)
SLUB: allocated new slab for cache 'kmalloc-2048' (size=2048, objects=2)
SLUB: allocated 2048 bytes from new slab (cache 'kmalloc-2048', actual size=2048)
SLUB: allocated 2048 bytes from cache 'kmalloc-2048' (actual size=2048)
slub_page_check: allocated 2 objects (should fill one page): ffffffffc0359000, ffffffffc0359800
SLUB: allocated new slab for cache 'kmalloc-2048' (size=2048, objects=2)
SLUB: allocated 2048 bytes from new slab (cache 'kmalloc-2048', actual size=2048)
slub_page_check: allocated 3rd object (should be in new page): ffffffffc035a000
SLUB: freed 2048 bytes to cache 'kmalloc-2048' (actual size=2048), released empty page
slub_page_check: freed 3rd object (should free entire page)
SLUB: freed 2048 bytes to cache 'kmalloc-2048' (actual size=2048), moved from full to partial
SLUB: freed 2048 bytes to cache 'kmalloc-2048' (actual size=2048), released empty page
slub_page_check: freed 1st and 2nd objects
slub_page_check passed!

--- Large Allocation Check ---
SLUB: large allocation 4196 bytes -> 2 pages
slub_large_check: allocated 4196 bytes at ffffffffc0359000 (page aligned)
SLUB: freed large allocation 4196 bytes (2 pages)
slub_large_check: freed large allocation
slub_large_check passed!

--- Mixed Scenario Check ---
SLUB: created cache 'kmalloc-128' (size=128)
SLUB: allocated new slab for cache 'kmalloc-128' (size=128, objects=32)
SLUB: allocated 100 bytes from new slab (cache 'kmalloc-128', actual size=128)
SLUB: created cache 'kmalloc-512' (size=512)
SLUB: allocated new slab for cache 'kmalloc-512' (size=512, objects=8)
SLUB: allocated 500 bytes from new slab (cache 'kmalloc-512', actual size=512)
SLUB: allocated new slab for cache 'kmalloc-2048' (size=2048, objects=2)
SLUB: allocated 2048 bytes from new slab (cache 'kmalloc-2048', actual size=2048)
slub_mixed_check: allocated mixed sizes: 100B@ffffffffc0359000, 500B@ffffffffc035a000, 2048B@ffffffffc035b000
SLUB: freed 100 bytes to cache 'kmalloc-128' (actual size=128), released empty page
SLUB: freed 500 bytes to cache 'kmalloc-512' (actual size=512), released empty page
SLUB: freed 2048 bytes to cache 'kmalloc-2048' (actual size=2048), released empty page
slub_mixed_check: freed all mixed allocations
slub_mixed_check passed!

=== SLUB All Tests Passed ===
=== SLUB Test Complete ===
```
