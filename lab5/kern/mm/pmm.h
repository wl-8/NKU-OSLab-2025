#ifndef __KERN_MM_PMM_H__
#define __KERN_MM_PMM_H__

#include <defs.h>
#include <mmu.h>
#include <memlayout.h>
#include <atomic.h>
#include <assert.h>

/* *
 * pmm_manager 是物理内存管理器的抽象类
 * 具体的物理内存管理器（如 first_fit、best_fit 等）只需实现该类中的方法
 * 就可以被 ucore 用来管理整个物理内存空间
 * 
 * pmm_manager is a physical memory management class. A special pmm manager - XXX_pmm_manager
 * only needs to implement the methods in pmm_manager class, then XXX_pmm_manager can be used
 * by ucore to manage the total physical memory space.
 */
struct pmm_manager
{
    const char *name;                                 // 内存管理器的名称 (XXX_pmm_manager's name)
    void (*init)(void);                               // 初始化内部数据结构（空闲块链表、空闲块数量等）
                                                      // initialize internal description&management data structure
                                                      // (free block list, number of free block) of XXX_pmm_manager
    void (*init_memmap)(struct Page *base, size_t n); // 根据初始的空闲物理内存空间建立管理数据结构
                                                      // setup description&management data structcure according to
                                                      // the initial free physical memory space
    struct Page *(*alloc_pages)(size_t n);            // 分配至少 n 个页面，具体取决于分配算法
                                                      // allocate >=n pages, depend on the allocation algorithm
    void (*free_pages)(struct Page *base, size_t n);  // 释放至少 n 个页面，base 是页描述符的地址
                                                      // free >=n pages with "base" addr of Page descriptor structures(memlayout.h)
    size_t (*nr_free_pages)(void);                    // 返回空闲页面的数量
                                                      // return the number of free pages
    void (*check)(void);                              // 检查内存管理器的正确性
                                                      // check the correctness of XXX_pmm_manager
};

// 全局变量声明 (Global variable declarations)
extern const struct pmm_manager *pmm_manager;  // 当前使用的物理内存管理器
extern pde_t *boot_pgdir_va;                   // 启动时页目录的虚拟地址
extern const size_t nbase;                     // 物理内存起始页号（RISC-V中从0x80000000开始）
extern uintptr_t boot_pgdir_pa;                // 启动时页目录的物理地址

// 物理内存管理初始化 (Physical memory management initialization)
void pmm_init(void);

// 页面分配和释放函数 (Page allocation and deallocation functions)
struct Page *alloc_pages(size_t n);  // 分配 n 个连续页面
void free_pages(struct Page *base, size_t n);  // 释放从 base 开始的 n 个页面
size_t nr_free_pages(void);          // 返回当前空闲页面数量

// 便捷宏：分配和释放单个页面 (Convenience macros for single page allocation/deallocation)
#define alloc_page() alloc_pages(1)
#define free_page(page) free_pages(page, 1)

// 页表管理函数 (Page table management functions)
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create);  // 获取线性地址 la 对应的页表项指针
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store);  // 获取线性地址对应的页面
void page_remove(pde_t *pgdir, uintptr_t la);  // 移除线性地址的映射
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);  // 建立物理页到线性地址的映射

// 其他辅助函数 (Other auxiliary functions)
void load_esp0(uintptr_t esp0);  // 加载内核栈指针
void tlb_invalidate(pde_t *pgdir, uintptr_t la);  // 使TLB中的某个条目失效
struct Page *pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm);  // 分配页面并建立映射
void unmap_range(pde_t *pgdir, uintptr_t start, uintptr_t end);  // 取消一段地址范围的映射
void exit_range(pde_t *pgdir, uintptr_t start, uintptr_t end);  // 释放一段地址范围的页表
int copy_range(pde_t *to, pde_t *from, uintptr_t start, uintptr_t end, bool share);  // 复制地址空间

void print_pgdir(void);  // 打印页目录信息

/* *
 * PADDR - 将内核虚拟地址转换为物理地址
 * 接受一个内核虚拟地址（大于KERNBASE的地址），返回对应的物理地址
 * 如果传入非内核虚拟地址会触发 panic
 * 
 * PADDR - takes a kernel virtual address (an address that points above KERNBASE),
 * where the machine's maximum 256MB of physical memory is mapped and returns the
 * corresponding physical address.  It panics if you pass it a non-kernel virtual address.
 */
#define PADDR(kva)                                                 \
    ({                                                             \
        uintptr_t __m_kva = (uintptr_t)(kva);                      \
        if (__m_kva < KERNBASE)                                    \
        {                                                          \
            panic("PADDR called with invalid kva %08lx", __m_kva); \
        }                                                          \
        __m_kva - va_pa_offset;                                    \
    })

/* *
 * KADDR - 将物理地址转换为内核虚拟地址
 * 接受一个物理地址，返回对应的内核虚拟地址
 * 如果传入无效的物理地址会触发 panic
 * 
 * KADDR - takes a physical address and returns the corresponding kernel virtual
 * address. It panics if you pass an invalid physical address.
 */
#define KADDR(pa)                                                \
    ({                                                           \
        uintptr_t __m_pa = (pa);                                 \
        size_t __m_ppn = PPN(__m_pa);                            \
        if (__m_ppn >= npage)                                    \
        {                                                        \
            panic("KADDR called with invalid pa %08lx", __m_pa); \
        }                                                        \
        (void *)(__m_pa + va_pa_offset);                         \
    })

// 全局变量：物理页面数组和数量 (Global variables: physical page array and count)
extern struct Page *pages;       // 物理页面描述符数组
extern size_t npage;            // 物理页面总数
extern uint_t va_pa_offset;     // 虚拟地址和物理地址的偏移量

/* *
 * page2ppn - 将页面结构体指针转换为物理页号
 * @page: 页面结构体指针
 * @return: 物理页号（Physical Page Number）
 */
static inline ppn_t
page2ppn(struct Page *page)
{
    return page - pages + nbase;
}

/* *
 * page2pa - 将页面结构体指针转换为物理地址
 * @page: 页面结构体指针
 * @return: 物理地址
 */
static inline uintptr_t
page2pa(struct Page *page)
{
    return page2ppn(page) << PGSHIFT;
}

/* *
 * pa2page - 将物理地址转换为页面结构体指针
 * @pa: 物理地址
 * @return: 对应的页面结构体指针
 */
static inline struct Page *
pa2page(uintptr_t pa)
{
    if (PPN(pa) >= npage)
    {
        panic("pa2page called with invalid pa");
    }
    return &pages[PPN(pa) - nbase];
}

/* *
 * page2kva - 将页面结构体指针转换为内核虚拟地址
 * @page: 页面结构体指针
 * @return: 对应的内核虚拟地址
 */
static inline void *
page2kva(struct Page *page)
{
    return KADDR(page2pa(page));
}

/* *
 * kva2page - 将内核虚拟地址转换为页面结构体指针
 * @kva: 内核虚拟地址
 * @return: 对应的页面结构体指针
 */
static inline struct Page *
kva2page(void *kva)
{
    return pa2page(PADDR(kva));
}

/* *
 * pte2page - 将页表项转换为页面结构体指针
 * @pte: 页表项
 * @return: 对应的页面结构体指针
 */
static inline struct Page *
pte2page(pte_t pte)
{
    if (!(pte & PTE_V))
    {
        panic("pte2page called with invalid pte");
    }
    return pa2page(PTE_ADDR(pte));
}

/* *
 * pde2page - 将页目录项转换为页面结构体指针
 * @pde: 页目录项
 * @return: 对应的页面结构体指针
 */
static inline struct Page *
pde2page(pde_t pde)
{
    return pa2page(PDE_ADDR(pde));
}

/* *
 * page_ref - 获取页面的引用计数
 * @page: 页面结构体指针
 * @return: 页面的引用计数
 */
static inline int
page_ref(struct Page *page)
{
    return page->ref;
}

/* *
 * set_page_ref - 设置页面的引用计数
 * @page: 页面结构体指针
 * @val: 要设置的引用计数值
 */
static inline void
set_page_ref(struct Page *page, int val)
{
    page->ref = val;
}

/* *
 * page_ref_inc - 增加页面的引用计数
 * @page: 页面结构体指针
 * @return: 增加后的引用计数
 */
static inline int
page_ref_inc(struct Page *page)
{
    page->ref += 1;
    return page->ref;
}

/* *
 * page_ref_dec - 减少页面的引用计数
 * @page: 页面结构体指针
 * @return: 减少后的引用计数
 */
static inline int
page_ref_dec(struct Page *page)
{
    page->ref -= 1;
    return page->ref;
}

/* *
 * flush_tlb - 刷新整个TLB（Translation Lookaside Buffer）
 * 使用RISC-V的sfence.vma指令清空所有TLB条目
 */
static inline void flush_tlb()
{
    asm volatile("sfence.vma");
}

/* *
 * pte_create - 根据物理页号和权限位构造页表项
 * @ppn: 物理页号
 * @type: 权限类型（读/写/执行等）
 * @return: 构造好的页表项
 * 
 * construct PTE from a page and permission bits
 */
static inline pte_t pte_create(uintptr_t ppn, int type)
{
    return (ppn << PTE_PPN_SHIFT) | PTE_V | type;
}

/* *
 * ptd_create - 创建页目录项（Page Table Directory entry）
 * @ppn: 物理页号
 * @return: 构造好的页目录项
 */
static inline pte_t ptd_create(uintptr_t ppn)
{
    return pte_create(ppn, PTE_V);
}

// 启动栈的起始和结束地址 (Boot stack start and end addresses)
extern char bootstack[], bootstacktop[];

#endif /* !__KERN_MM_PMM_H__ */
