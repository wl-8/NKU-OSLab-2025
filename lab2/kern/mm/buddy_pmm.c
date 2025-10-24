/*2313857*/
#include <pmm.h>              // 物理内存管理（Page 结构、宏、接口）
#include <list.h>             // 双向链表原语：list_init、list_add、list_del、list_next...
#include <string.h>
#include <buddy_pmm.h>     
#include <stdio.h>

typedef struct{
    struct Page* base;     // 块头指针
    size_t size;           // 整体容量（总页数）
    size_t valid_size;     // 可用容量
    int longest[65535];    // 节点数组，存储该节点覆盖的空闲页数（占用时置 0）
    unsigned int nr_free;
} buddyT;

#define LEFT_LEAF(index) ((index) * 2 + 1)
#define RIGHT_LEAF(index) ((index) * 2 + 2)
#define PARENT(index) ( ((index) + 1) / 2 - 1)

// 全局完全二叉树
buddyT bt;

// 判断块大小是否为 2 的幂
static bool isPowerOf2(size_t x){
    return (x > 0) && ((x & (x-1)) == 0);
}
// 向下取整 log2(x)
static size_t floor_log2(size_t x) {
    assert(x > 0);

    size_t res = 0;
    while (x > 1)
    {
        x >>= 1;
        res++;
    }
    return res;
}

// 初始化分配器
static void
buddy_init(void){
    bt.nr_free = 0;
    return;
}

// 初始化空闲区
static void 
buddy_init_memmap(struct Page* base, size_t n){
    assert(n > 0);                          // 初始分配至少 1 页

    size_t order = isPowerOf2(n) ? floor_log2(n) : floor_log2(n) + 1;        // 最近的 2 幂次阶（向上取整）
    size_t tSize = 2 * (1 << order) - 1;     // 完全二叉树节点数   
    
    bt.base = base;
    bt.size = 1 << order;
    bt.valid_size = n;

    // 页初始化
    struct Page* p = base;
    for(; p != base+n; p++){
        assert(PageReserved(p));            // 约定初始为 reserved
        p->flags=0;                         // 标志位置 0
        set_page_ref(p, 0);                 // 引用清空
    }
    bt.nr_free += n;
    
    // 树初始化
    // 不可用叶子节点状态位置 0
    for(int i = tSize - 1; i >= tSize - (bt.size - n); i--){
        bt.longest[i] = 0;
    }
    // 可用叶子节点状态位置 1
    for(int i = tSize - (bt.size - n) - 1; i >= tSize - bt.size; i--){
        bt.longest[i] = 1;
    }

    // 自底向上更新
    for(int i = tSize - bt.size - 1; i >= 0; i--){
        size_t left = bt.longest[2 * i + 1];
        size_t right = bt.longest[2 * i + 2];

        if(left == right){
            // 伙伴空闲块可合并
            bt.longest[i] = left ? left << 1 : 0;
        }
        else{
            bt.longest[i] = (left > right) ? left : right;
        }
    }

    return;
}

// 分配算法
static struct Page *
buddy_alloc_pages(size_t n){
    assert(n > 0);

    // 最小分配空间
    size_t order = isPowerOf2(n) ? floor_log2(n) : floor_log2(n) + 1;
    size_t alloc_size = 1 << order;

    // 检查剩余空间
    if(bt.longest[0] < alloc_size){
        return NULL;
    }

    // 自顶向下搜索目标块
    size_t index = 0;           // 目标块节点索引
    size_t block_size;          // 目标块大小
    size_t offset = 0;          // 目标块起始偏移
    for(block_size=bt.size; block_size!=alloc_size; block_size/=2){
        if(bt.longest[LEFT_LEAF(index)] >= alloc_size)
            index = LEFT_LEAF(index);
        else
            index = RIGHT_LEAF(index);
    }

    bt.longest[index] = 0;                                  // 标记为占用
    offset = (index + 1) * block_size - bt.size;            // 计算占用块起始偏移
    bt.nr_free -=  alloc_size;

    // 自底向上更新
    while(index){
        index = PARENT(index);
        // 更新为子块的较大块
        bt.longest[index] = (bt.longest[LEFT_LEAF(index)] > bt.longest[RIGHT_LEAF(index)])
                    ? bt.longest[LEFT_LEAF(index)] : bt.longest[RIGHT_LEAF(index)];
    }

    return bt.base + offset;
}

// 回收&合并算法
static void
buddy_free_pages(struct Page* base, size_t n){
    assert(n > 0);

    // 最小释放空间
    size_t order = isPowerOf2(n) ? floor_log2(n) : floor_log2(n) + 1;
    size_t free_size = 1 << order;

    size_t offset = base - bt.base;         // 待释放块偏移位置
    size_t index = offset + bt.size - 1;    // 待释放块索引位置
    size_t mem = index;

    // 检查合法性 1: 是否非法释放不可用节点
    assert(offset < bt.valid_size);
    assert(offset + free_size <= bt.valid_size);

    // 自底向上寻找被占用节点
    size_t block_size = 1;
    // for(; bt.longest[index]!=0 && block_size < free_size; index=PARENT(index)){
    //     block_size <<= 1;
    //     if(index == 0) return;
    // }
    while(bt.longest[index]!=0 && index > 0){     
        index = PARENT(index);
        block_size <<= 1;
    }

    bt.longest[index] = block_size;

    // 检查合法性 2
    assert(block_size == free_size);
    assert(((index + 1) * block_size - bt.size) == offset);

    // 自底向上更新
    while(index){
        index = PARENT(index);

        size_t left = bt.longest[LEFT_LEAF(index)];
        size_t right = bt.longest[RIGHT_LEAF(index)];

        if(left == right) 
            bt.longest[index] = left ? (left << 1) : 0;
        else
            bt.longest[index] = (left > right) ? left : right;
    }

    // 更新页状态
    struct Page* p = base;
    for(; p != base + free_size; p++){
        assert(!PageReserved(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }
    bt.nr_free += free_size;

    return;
}

// 统计空闲页数
static size_t 
buddy_nr_free_pages(void){
    return bt.nr_free;
}

// 功能性测试
static void
buddy_check(void){
    cprintf("========== Buddy Test START ==========\n");

    size_t free0 = nr_free_pages();

    // 1) 基础检查：基本页分配与回收
    cprintf("[1] Basic check\n");
    struct Page *p0 = NULL, *p1 = NULL, *p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    // 唯一性 & 元数据
    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);
    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    // 算法逻辑正确性检验
    assert(p1 == p0 + 1 || p2 == p1 + 1);

    // 释放复原
    free_page(p0);
    free_page(p1);
    free_page(p2);
    assert(nr_free_pages() == free0);

    // 2) 占用冲突处理检查：可分配的最大 2^k 连续块（k=floor(log2(nr_free)))
    cprintf("[2] Conflict handler check\n");
    size_t nfree = nr_free_pages();             // 空闲页数
    size_t maxblk = 1 << floor_log2(nfree);     // 最大可分配块 (2^k)
    struct Page *big = alloc_pages(maxblk);
    assert(big != NULL);

    // 最大块占用
    struct Page *big2 = alloc_pages(maxblk);
    assert(big2 == NULL);

    // 释放复原
    free_pages(big, maxblk);
    assert(nr_free_pages() == free0);

    // 3) 分裂/合并检查：
    cprintf("[3] Alloc&Release&Merge check\n");
    // 分配 2 页连续块
    struct Page *Q = alloc_pages(2);         
    assert(Q != NULL);
    free_pages(Q, 2);                         

    // 分配单页
    struct Page *s0 = alloc_page();           
    struct Page *s1 = alloc_page();
    assert(s0 != NULL && s1 != NULL);

    // 算法正确性检验
    assert(s0 == Q && s1 == Q + 1);
    free_page(s0);
    free_page(s1);

    // 合并检查
    struct Page *QQ = alloc_pages(2);         
    assert(QQ == Q);
    free_pages(QQ, 2);
    assert(nr_free_pages() == free0);

    // 4) 非 2 的幂内存请求
    cprintf("[4] Special request check\n");
    struct Page *np = alloc_pages(3);         
    assert(np != NULL);
    // 简单验证：此时空闲减少了 4
    assert(nr_free_pages() + 4 == free0);
    free_pages(np, 3);                       
    assert(nr_free_pages() == free0);

    // 5) 并发请求检查
    cprintf("[5] Concurrent request check\n");
    struct Page *a = alloc_pages(1);
    struct Page *b = alloc_pages(2);
    struct Page *c = alloc_pages(4);
    assert(a && b && c);
    free_pages(b, 2);
    free_pages(a, 1);
    struct Page *d = alloc_pages(2);
    assert(d != NULL);                        
    free_pages(c, 4);
    free_pages(d, 2);
    assert(nr_free_pages() == free0);

    cprintf("========== Buddy Test PASS ==========\n");
}

// 实例化
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};