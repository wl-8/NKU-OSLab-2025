/**
 * @file pmm.c
 * @brief 物理内存管理器的实现 (Physical Memory Manager Implementation)
 * 
 * 该文件实现了物理内存管理的核心功能，包括：
 * - 物理页面的分配和释放
 * - 页表的创建和管理
 * - 地址空间的映射和复制
 * - 内存初始化和检查
 */

#include <default_pmm.h>
#include <defs.h>
#include <error.h>
#include <kmalloc.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <dtb.h>
#include <stdio.h>
#include <string.h>
#include <sync.h>
#include <vmm.h>
#include <riscv.h>

// ==================== 全局变量定义 (Global Variable Definitions) ====================

// 物理页面数组的虚拟地址 (virtual address of physical page array)
struct Page *pages;

// 物理内存的总页数 (amount of physical memory in pages)
size_t npage = 0;

// 虚拟地址和物理地址的偏移量，内核镜像映射在 VA=KERNBASE 和 PA=info.base
// (The kernel image is mapped at VA=KERNBASE and PA=info.base)
uint_t va_pa_offset;

// 物理内存起始页号，RISC-V 中内存从 0x80000000 开始
// (memory starts at 0x80000000 in RISC-V)
const size_t nbase = DRAM_BASE / PGSIZE;

// 启动时页目录的虚拟地址 (virtual address of boot-time page directory)
pde_t *boot_pgdir_va = NULL;

// 启动时页目录的物理地址 (physical address of boot-time page directory)
uintptr_t boot_pgdir_pa;

// 物理内存管理器指针 (physical memory manager)
const struct pmm_manager *pmm_manager;

// ==================== 内部函数声明 (Internal Function Declarations) ====================

static void check_alloc_page(void);    // 检查页面分配功能
static void check_pgdir(void);         // 检查页目录功能
static void check_boot_pgdir(void);    // 检查启动页目录

/**
 * init_pmm_manager - 初始化物理内存管理器实例
 * 
 * 设置当前使用的物理内存管理器，并调用其初始化函数
 */
static void init_pmm_manager(void)
{
    pmm_manager = &default_pmm_manager;  // 使用默认的内存管理器
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();  // 调用管理器的初始化函数
}

/**
 * init_memmap - 为空闲内存建立 Page 结构体
 * @base: 起始页面的指针
 * @n: 页面数量
 * 
 * 调用 pmm->init_memmap 来初始化空闲内存的管理结构
 */
static void init_memmap(struct Page *base, size_t n)
{
    pmm_manager->init_memmap(base, n);
}

/**
 * alloc_pages - 分配 n 个连续的物理页面
 * @n: 要分配的页面数量
 * @return: 成功返回第一个页面的指针，失败返回 NULL
 * 
 * 调用 pmm->alloc_pages 来分配 n*PAGESIZE 大小的连续内存
 * 使用中断禁用来保证原子性操作
 */
struct Page *alloc_pages(size_t n)
{
    struct Page *page = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);  // 保存中断状态并禁用中断
    {
        page = pmm_manager->alloc_pages(n);  // 调用具体的分配算法
    }
    local_intr_restore(intr_flag);  // 恢复中断状态
    return page;
}

/**
 * free_pages - 释放 n 个连续的物理页面
 * @base: 要释放的第一个页面的指针
 * @n: 要释放的页面数量
 * 
 * 调用 pmm->free_pages 来释放 n*PAGESIZE 大小的连续内存
 * 使用中断禁用来保证原子性操作
 */
void free_pages(struct Page *base, size_t n)
{
    bool intr_flag;
    local_intr_save(intr_flag);  // 保存中断状态并禁用中断
    {
        pmm_manager->free_pages(base, n);  // 调用具体的释放算法
    }
    local_intr_restore(intr_flag);  // 恢复中断状态
}

/**
 * nr_free_pages - 获取当前空闲内存的大小
 * @return: 空闲页面的数量
 * 
 * 调用 pmm->nr_free_pages 来获取当前空闲内存的大小（nr*PAGESIZE）
 * 使用中断禁用来保证原子性操作
 */
size_t nr_free_pages(void)
{
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);  // 保存中断状态并禁用中断
    {
        ret = pmm_manager->nr_free_pages();  // 获取空闲页面数
    }
    local_intr_restore(intr_flag);  // 恢复中断状态
    return ret;
}

/**
 * page_init - 初始化物理内存管理
 * 
 * 该函数完成以下任务：
 * 1. 设置虚拟地址和物理地址的偏移量
 * 2. 从 DTB 获取物理内存信息
 * 3. 初始化页面数组 (pages)
 * 4. 标记已使用的内存页为保留
 * 5. 建立空闲页面链表
 */
static void page_init(void)
{
    extern char kern_entry[];

    // 设置虚拟地址和物理地址的偏移量
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // 从设备树 (DTB) 获取内存信息
    uint64_t mem_begin = get_memory_base();
    uint64_t mem_size = get_memory_size();
    if (mem_size == 0)
    {
        panic("DTB memory info not available");  // 如果获取不到内存信息则触发 panic
    }
    uint64_t mem_end = mem_begin + mem_size;

    // 打印物理内存布局信息
    cprintf("physcial memory map:\n");
    cprintf("  memory: 0x%08lx, [0x%08lx, 0x%08lx].\n", mem_size, mem_begin,
            mem_end - 1);

    // 计算最大物理地址，不超过 KERNTOP
    uint64_t maxpa = mem_end;

    if (maxpa > KERNTOP)
    {
        maxpa = KERNTOP;
    }

    extern char end[];  // 内核结束地址

    // 计算总页数
    npage = maxpa / PGSIZE;
    
    // BBL 已经把初始页表放在内核后的第一个可用页面
    // 所以需要在 end 后面加上额外的偏移来避开它
    // (BBL has put the initial page table at the first available page after the kernel
    // so stay away from it by adding extra offset to end)
    pages = (struct Page *)ROUNDUP((void *)end, PGSIZE);

    // 将所有页面初始化为保留状态
    for (size_t i = 0; i < npage - nbase; i++)
    {
        SetPageReserved(pages + i);
    }

    // 计算空闲内存的起始地址（pages 数组后面）
    uintptr_t freemem = PADDR((uintptr_t)pages + sizeof(struct Page) * (npage - nbase));

    // 对齐到页边界
    mem_begin = ROUNDUP(freemem, PGSIZE);
    mem_end = ROUNDDOWN(mem_end, PGSIZE);
    
    // 建立空闲页面列表
    if (freemem < mem_end)
    {
        init_memmap(pa2page(mem_begin), (mem_end - mem_begin) / PGSIZE);
    }
    cprintf("vapaofset is %llu\n", va_pa_offset);
}

/**
 * boot_map_segment - 建立段内存的线性地址到物理地址的映射
 * @pgdir: 页目录的内核虚拟地址
 * @la: 需要映射的线性地址
 * @size: 内存大小
 * @pa: 对应的物理地址
 * @perm: 该内存的权限位
 * 
 * 该函数用于在启动阶段建立页表映射
 * 注意：la 和 pa 的页内偏移必须相同
 */
static void boot_map_segment(pde_t *pgdir, uintptr_t la, size_t size,
                             uintptr_t pa, uint32_t perm)
{
    assert(PGOFF(la) == PGOFF(pa));  // 确保 la 和 pa 的页内偏移相同
    
    // 计算需要映射的页数
    size_t n = ROUNDUP(size + PGOFF(la), PGSIZE) / PGSIZE;
    
    // 将 la 和 pa 对齐到页边界
    la = ROUNDDOWN(la, PGSIZE);
    pa = ROUNDDOWN(pa, PGSIZE);
    
    // 为每个页面建立映射
    for (; n > 0; n--, la += PGSIZE, pa += PGSIZE)
    {
        pte_t *ptep = get_pte(pgdir, la, 1);  // 获取或创建页表项
        assert(ptep != NULL);
        *ptep = pte_create(pa >> PGSHIFT, PTE_V | perm);  // 设置页表项
    }
}

/**
 * boot_alloc_page - 在启动阶段分配一个页面
 * @return: 分配的页面的内核虚拟地址
 * 
 * 使用 pmm->alloc_pages(1) 分配一个页面
 * 这个函数用于为页目录表(PDT)和页表(PT)获取内存
 */
static void *boot_alloc_page(void)
{
    struct Page *p = alloc_page();
    if (p == NULL)
    {
        panic("boot_alloc_page failed.\n");  // 分配失败则触发 panic
    }
    return page2kva(p);  // 返回内核虚拟地址
}

/**
 * pmm_init - 初始化物理内存管理器
 * 
 * 该函数完成以下任务：
 * 1. 初始化物理内存管理器 (pmm_manager)
 * 2. 检测物理内存空间，保留已使用的内存
 * 3. 创建空闲页面列表
 * 4. 建立页目录和页表来启用分页机制
 * 5. 检查 pmm 和分页机制的正确性
 */
void pmm_init(void)
{
    // 我们需要分配/释放物理内存（粒度是 4KB 或其他大小）
    // 所以在 pmm.h 中定义了物理内存管理器框架 (struct pmm_manager)
    // 首先我们应该基于该框架初始化一个物理内存管理器(pmm)
    // 然后 pmm 就可以分配/释放物理内存
    // 现在可用的有 first_fit/best_fit/worst_fit/buddy_system 等 pmm
    // (We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.)
    init_pmm_manager();

    // 检测物理内存空间，保留已使用的内存，
    // 然后使用 pmm->init_memmap 创建空闲页面列表
    // (detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list)
    page_init();

    // 使用 pmm->check 验证 pmm 中分配/释放函数的正确性
    // (use pmm->check to verify the correctness of the alloc/free function in a pmm)
    check_alloc_page();

    // 创建 boot_pgdir，一个初始的页目录 (Page Directory Table, PDT)
    // (create boot_pgdir, an initial page directory(Page Directory Table, PDT))
    extern char boot_page_table_sv39[];
    boot_pgdir_va = (pte_t *)boot_page_table_sv39;
    boot_pgdir_pa = PADDR(boot_pgdir_va);

    // 检查页目录的正确性
    check_pgdir();

    // 确保 KERNBASE 和 KERNTOP 都对齐到 PTSIZE
    static_assert(KERNBASE % PTSIZE == 0 && KERNTOP % PTSIZE == 0);

    // 现在基本的虚拟内存映射已经建立（参见 memalyout.h）
    // 检查基本虚拟内存映射的正确性
    // (now the basic virtual memory map(see memalyout.h) is established.
    // check the correctness of the basic virtual memory map.)
    check_boot_pgdir();

    // 初始化内核内存分配器
    kmalloc_init();
}

/**
 * get_pte - 获取线性地址 la 对应的页表项的内核虚拟地址
 * @pgdir: 页目录的内核虚拟基地址
 * @la: 需要映射的线性地址
 * @create: 是否分配一个页面作为页表
 * @return: 该页表项的内核虚拟地址，失败返回 NULL
 * 
 * 如果包含此页表项的页表不存在，根据 create 参数决定是否分配页面
 * RISC-V 使用三级页表，需要遍历两级页目录
 */
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create)
{
    // 第一级页目录项（顶层页目录）
    pde_t *pdep1 = &pgdir[PDX1(la)];
    if (!(*pdep1 & PTE_V))  // 如果第一级页目录项无效
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;  // 不创建或分配失败，返回 NULL
        }
        set_page_ref(page, 1);  // 设置引用计数为 1
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);  // 清空页面内容
        *pdep1 = pte_create(page2ppn(page), PTE_U | PTE_V);  // 创建页目录项
    }

    // 第二级页目录项（中间层页目录）
    pde_t *pdep0 = &((pde_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)];
    if (!(*pdep0 & PTE_V))  // 如果第二级页目录项无效
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;  // 不创建或分配失败，返回 NULL
        }
        set_page_ref(page, 1);  // 设置引用计数为 1
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);  // 清空页面内容
        *pdep0 = pte_create(page2ppn(page), PTE_U | PTE_V);  // 创建页目录项
    }
    // 返回第三级页表项的地址
    return &((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)];
}

/**
 * get_page - 使用页目录 pgdir 获取线性地址 la 对应的 Page 结构体
 * @pgdir: 页目录的内核虚拟地址
 * @la: 线性地址
 * @ptep_store: 用于存储页表项指针的地址，可为 NULL
 * @return: 对应的 Page 结构体指针，失败返回 NULL
 */
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store)
{
    pte_t *ptep = get_pte(pgdir, la, 0);  // 获取页表项，不创建
    if (ptep_store != NULL)
    {
        *ptep_store = ptep;  // 如果需要，存储页表项指针
    }
    if (ptep != NULL && *ptep & PTE_V)  // 如果页表项存在且有效
    {
        return pte2page(*ptep);  // 返回对应的页面
    }
    return NULL;
}

/**
 * page_remove_pte - 释放与线性地址 la 相关的 Page 结构体
 * @pgdir: 页目录的内核虚拟地址
 * @la: 线性地址
 * @ptep: 页表项指针
 * 
 * 清空（使无效）与线性地址 la 相关的页表项
 * 注意：页表被修改，因此需要使 TLB 无效
 */
static inline void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep)
{
    if (*ptep & PTE_V)  // 如果页表项有效
    {
        struct Page *page = pte2page(*ptep);  // 获取对应的页面
        page_ref_dec(page);  // 减少引用计数
        if (page_ref(page) == 0)  // 如果引用计数为 0
        {
            free_page(page);  // 释放页面
        }
        *ptep = 0;  // 清空页表项
        tlb_invalidate(pgdir, la);  // 使 TLB 中的对应条目无效
    }
}

/**
 * unmap_range - 取消一段地址范围的映射
 * @pgdir: 页目录的内核虚拟地址
 * @start: 起始地址（必须页对齐）
 * @end: 结束地址（必须页对齐）
 * 
 * 移除指定范围内的所有页表项，但不释放页表本身
 */
void unmap_range(pde_t *pgdir, uintptr_t start, uintptr_t end)
{
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);  // 确保地址页对齐
    assert(USER_ACCESS(start, end));  // 确保在用户地址空间范围内

    do
    {
        pte_t *ptep = get_pte(pgdir, start, 0);  // 获取页表项，不创建
        if (ptep == NULL)  // 如果页表不存在
        {
            // 跳过整个页表覆盖的范围
            start = ROUNDDOWN(start + PTSIZE, PTSIZE);
            continue;
        }
        if (*ptep != 0)  // 如果页表项不为空
        {
            page_remove_pte(pgdir, start, ptep);  // 移除页表项
        }
        start += PGSIZE;  // 移动到下一个页面
    } while (start != 0 && start < end);
}

/**
 * exit_range - 释放一段地址范围的页表
 * @pgdir: 页目录的内核虚拟地址
 * @start: 起始地址（必须页对齐）
 * @end: 结束地址（必须页对齐）
 * 
 * 释放指定范围内的页表和页目录页（如果它们已经全部无效）
 * 这是一个三级页表的释放过程
 */
void exit_range(pde_t *pgdir, uintptr_t start, uintptr_t end)
{
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);  // 确保地址页对齐
    assert(USER_ACCESS(start, end));  // 确保在用户地址空间范围内

    uintptr_t d1start, d0start;
    int free_pt, free_pd0;
    pde_t *pd0, *pt, pde1, pde0;
    d1start = ROUNDDOWN(start, PDSIZE);  // 第一级页目录的起始地址
    d0start = ROUNDDOWN(start, PTSIZE);  // 第二级页目录的起始地址
    do
    {
        // 第一级页目录项 (level 1 page directory entry)
        pde1 = pgdir[PDX1(d1start)];
        // 如果有有效项，进入第二级 (level 0)
        // 尝试释放第二级页目录中所有有效项指向的所有页表
        // 然后尝试释放该第二级页目录并更新第一级项
        // (if there is a valid entry, get into level 0
        // and try to free all page tables pointed to by
        // all valid entries in level 0 page directory,
        // then try to free this level 0 page directory
        // and update level 1 entry)
        if (pde1 & PTE_V)
        {
            pd0 = page2kva(pde2page(pde1));  // 获取第二级页目录
            // 尝试释放所有页表 (try to free all page tables)
            free_pd0 = 1;
            do
            {
                pde0 = pd0[PDX0(d0start)];
                if (pde0 & PTE_V)
                {
                    pt = page2kva(pde2page(pde0));  // 获取页表
                    // 尝试释放页表 (try to free page table)
                    free_pt = 1;
                    for (int i = 0; i < NPTEENTRY; i++)
                        if (pt[i] & PTE_V)  // 如果有有效项
                        {
                            free_pt = 0;  // 不能释放
                            break;
                        }
                    // 只有当所有项都已经无效时才释放 (free it only when all entry are already invalid)
                    if (free_pt)
                    {
                        free_page(pde2page(pde0));
                        pd0[PDX0(d0start)] = 0;
                    }
                }
                else
                    free_pd0 = 0;  // 如果有无效的项，不能释放第二级页目录
                d0start += PTSIZE;
            } while (d0start != 0 && d0start < d1start + PDSIZE && d0start < end);
            // 只有当第二级页目录中所有 pde0 都已经无效时才释放
            // (free level 0 page directory only when all pde0s in it are already invalid)
            if (free_pd0)
            {
                free_page(pde2page(pde1));
                pgdir[PDX1(d1start)] = 0;
            }
        }
        d1start += PDSIZE;
        d0start = d1start;
    } while (d1start != 0 && d1start < end);
}
/**
 * copy_range - 复制进程 A 的内存内容 (start, end) 到进程 B
 * @to: 进程 B 的页目录地址
 * @from: 进程 A 的页目录地址
 * @start: 起始地址
 * @end: 结束地址
 * @share: 标志位，指示是复制还是共享（这里仅使用复制方法）
 * @return: 0 表示成功，负数表示错误
 * 
 * 调用图：copy_mm-->dup_mmap-->copy_range
 * (CALL GRAPH: copy_mm-->dup_mmap-->copy_range)
 */
int copy_range(pde_t *to, pde_t *from, uintptr_t start, uintptr_t end,
               bool share)
{
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);  // 确保地址页对齐
    assert(USER_ACCESS(start, end));  // 确保在用户地址空间范围内
    
    // 按页面单位复制内容 (copy content by page unit)
    do
    {
        // 调用 get_pte 根据地址 start 查找进程 A 的页表项
        // (call get_pte to find process A's pte according to the addr start)
        pte_t *ptep = get_pte(from, start, 0), *nptep;
        if (ptep == NULL)  // 如果页表不存在
        {
            // 跳过整个页表覆盖的范围
            start = ROUNDDOWN(start + PTSIZE, PTSIZE);
            continue;
        }
        
        // 调用 get_pte 根据地址 start 查找进程 B 的页表项
        // 如果 pte 为 NULL，则分配一个页表
        // (call get_pte to find process B's pte according to the addr start.
        // If pte is NULL, just alloc a PT)
        if (*ptep & PTE_V)  // 如果页表项有效
        {
            if ((nptep = get_pte(to, start, 1)) == NULL)
            {
                return -E_NO_MEM;  // 分配失败，返回内存不足错误
            }
            uint32_t perm = (*ptep & PTE_USER);  // 获取权限位
            
            // 从 ptep 获取页面 (get page from ptep)
            struct Page *page = pte2page(*ptep);
            
            // 为进程 B 分配一个页面 (alloc a page for process B)
            struct Page *npage = alloc_page();
            assert(page != NULL);
            assert(npage != NULL);
            int ret = 0;
            
            /* LAB5:EXERCISE2 YOUR CODE 2313857
             * 将 page 的内容复制到 npage，并建立 npage 的物理地址与线性地址 start 的映射
             * (replicate content of page to npage, build the map of phy addr of
             * nage with the linear addr start)
             *
             * 一些有用的宏和定义：
             * (Some Useful MACROs and DEFINEs, you can use them in below implementation.)
             * 宏或函数：
             * (MACROs or Functions:)
             *    page2kva(struct Page *page): 返回页面管理的内存的内核虚拟地址
             *                                 (return the kernel vritual addr of
             *                                 memory which page managed (SEE pmm.h))
             *    page_insert: 建立一个 Page 的物理地址与线性地址 la 的映射
             *                 (build the map of phy addr of an Page with the linear addr la)
             *    memcpy: 典型的内存复制函数
             *            (typical memory copy function)
             *
             * (1) 找到 src_kvaddr: page 的内核虚拟地址
             *     (find src_kvaddr: the kernel virtual address of page)
             * (2) 找到 dst_kvaddr: npage 的内核虚拟地址
             *     (find dst_kvaddr: the kernel virtual address of npage)
             * (3) 从 src_kvaddr 复制内存到 dst_kvaddr，大小为 PGSIZE
             *     (memory copy from src_kvaddr to dst_kvaddr, size is PGSIZE)
             * (4) 建立 npage 的物理地址与线性地址 start 的映射
             *     (build the map of phy addr of  nage with the linear addr start)
             */

            // (1) 获取源页面（进程A的页面）的内核虚拟地址
            // page2kva 将 Page 结构体指针转换为对应物理页面的内核虚拟地址
            // 这样我们就可以访问该页面的内容了
            void *src_kvaddr = page2kva(page);
            
            // (2) 获取目标页面（进程B的新页面）的内核虚拟地址
            // 同样使用 page2kva 转换新分配的页面地址
            void *dst_kvaddr = page2kva(npage);
            
            // (3) 将源页面的内容复制到目标页面
            // memcpy(目标地址, 源地址, 复制大小)
            // PGSIZE 是一个页面的大小（通常是 4KB）
            // 这一步实现了进程地址空间内容的实际复制
            memcpy(dst_kvaddr, src_kvaddr, PGSIZE);
            
            // (4) 建立目标页面到进程B地址空间的映射
            // page_insert 在进程B的页表中建立映射关系：
            // - to: 进程B的页目录
            // - npage: 新分配的物理页面
            // - start: 要映射到的虚拟地址（与进程A中相同的虚拟地址）
            // - perm: 页面权限（从进程A的页表项中复制而来）
            // 这样进程B就可以通过虚拟地址 start 访问到复制的内容了
            ret = page_insert(to, npage, start, perm);

            assert(ret == 0);
        }
        start += PGSIZE;  // 移动到下一个页面
    } while (start != 0 && start < end);
    return 0;
}

/**
 * page_remove - 释放与线性地址 la 相关的页面
 * @pgdir: 页目录的内核虚拟地址
 * @la: 线性地址
 * 
 * 释放与线性地址 la 相关的页面，前提是该页面有有效的页表项
 */
void page_remove(pde_t *pgdir, uintptr_t la)
{
    pte_t *ptep = get_pte(pgdir, la, 0);  // 获取页表项，不创建
    if (ptep != NULL)
    {
        page_remove_pte(pgdir, la, ptep);  // 移除页表项
    }
}

/**
 * page_insert - 建立一个 Page 的物理地址与线性地址 la 的映射
 * @pgdir: 页目录的内核虚拟基地址
 * @page: 需要映射的 Page
 * @la: 需要映射的线性地址
 * @perm: 该页面的权限，将设置在相关的 pte 中
 * @return: 0 表示成功，-E_NO_MEM 表示内存不足
 * 
 * 注意：页表被修改，因此需要使 TLB 无效
 */
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)
{
    pte_t *ptep = get_pte(pgdir, la, 1);  // 获取或创建页表项
    if (ptep == NULL)
    {
        return -E_NO_MEM;  // 分配失败
    }
    page_ref_inc(page);  // 增加页面引用计数
    
    if (*ptep & PTE_V)  // 如果页表项已经有效
    {
        struct Page *p = pte2page(*ptep);
        if (p == page)  // 如果是同一个页面
        {
            page_ref_dec(page);  // 减少引用计数（因为上面增加了）
        }
        else  // 如果是不同的页面
        {
            page_remove_pte(pgdir, la, ptep);  // 移除原有的页表项
        }
    }
    *ptep = pte_create(page2ppn(page), PTE_V | perm);  // 设置新的页表项
    tlb_invalidate(pgdir, la);  // 使 TLB 中的对应条目无效
    return 0;
}

/**
 * tlb_invalidate - 使 TLB 中的一个条目无效
 * @pgdir: 页目录的内核虚拟地址
 * @la: 线性地址
 * 
 * 使 TLB 中的一个条目无效，但只有当正在编辑的页表是处理器当前使用的页表时才有效
 * 使用 RISC-V 的 sfence.vma 指令来使指定地址的 TLB 条目无效
 */
void tlb_invalidate(pde_t *pgdir, uintptr_t la)
{
    asm volatile("sfence.vma %0" : : "r"(la));
}

/**
 * pgdir_alloc_page - 分配页面并建立映射
 * @pgdir: 页目录的内核虚拟地址
 * @la: 线性地址
 * @perm: 权限位
 * @return: 分配的页面，失败返回 NULL
 * 
 * 调用 alloc_page 和 page_insert 函数来：
 * 1. 分配一个页面大小的内存
 * 2. 建立线性地址 la 与物理地址 pa 的映射
 */
struct Page *pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm)
{
    struct Page *page = alloc_page();  // 分配一个页面
    if (page != NULL)
    {
        if (page_insert(pgdir, page, la, perm) != 0)  // 建立映射
        {
            free_page(page);  // 如果失败，释放页面
            return NULL;
        }
        // 为页面置换机制设置虚拟地址
        // swap_map_swappable(check_mm_struct, la, page, 0);
        page->pra_vaddr = la;
        assert(page_ref(page) == 1);  // 确保引用计数为 1
        // cprintf("get No. %d  page: pra_vaddr %x, pra_link.prev %x,
        // pra_link_next %x in pgdir_alloc_page\n", (page-pages),
        // page->pra_vaddr,page->pra_page_link.prev,
        // page->pra_page_link.next);
    }

    return page;
}

/**
 * check_alloc_page - 检查页面分配功能
 * 
 * 调用物理内存管理器的检查函数来验证页面分配和释放功能的正确性
 */
static void check_alloc_page(void)
{
    pmm_manager->check();  // 调用管理器的检查函数
    cprintf("check_alloc_page() succeeded!\n");
}

/**
 * check_pgdir - 检查页目录功能
 * 
 * 该函数测试页目录和页表的各种操作，包括：
 * - 页面插入和移除
 * - 引用计数管理
 * - 权限位设置
 * - 页表项查找
 */
static void check_pgdir(void)
{
    // assert(npage <= KMEMSIZE / PGSIZE);
    // RISC-V 中内存从 2GB 开始
    // 所以 npage 总是大于 KMEMSIZE / PGSIZE
    // (The memory starts at 2GB in RISC-V
    // so npage is always larger than KMEMSIZE / PGSIZE)
    size_t nr_free_store;

    nr_free_store = nr_free_pages();  // 保存当前空闲页面数

    // 检查基本条件
    assert(npage <= KERNTOP / PGSIZE);
    assert(boot_pgdir_va != NULL && (uint32_t)PGOFF(boot_pgdir_va) == 0);
    assert(get_page(boot_pgdir_va, 0x0, NULL) == NULL);

    struct Page *p1, *p2;
    p1 = alloc_page();  // 分配第一个测试页面
    assert(page_insert(boot_pgdir_va, p1, 0x0, 0) == 0);  // 插入页面到地址 0x0

    pte_t *ptep;
    assert((ptep = get_pte(boot_pgdir_va, 0x0, 0)) != NULL);  // 获取页表项
    assert(pte2page(*ptep) == p1);  // 验证页表项指向 p1
    assert(page_ref(p1) == 1);  // 验证引用计数为 1

    // 手动遍历页表结构来验证 get_pte 的正确性
    ptep = (pte_t *)KADDR(PDE_ADDR(boot_pgdir_va[0]));
    ptep = (pte_t *)KADDR(PDE_ADDR(ptep[0])) + 1;
    assert(get_pte(boot_pgdir_va, PGSIZE, 0) == ptep);

    p2 = alloc_page();  // 分配第二个测试页面
    assert(page_insert(boot_pgdir_va, p2, PGSIZE, PTE_U | PTE_W) == 0);  // 插入页面到地址 PGSIZE
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(*ptep & PTE_U);  // 验证用户权限位
    assert(*ptep & PTE_W);  // 验证写权限位
    assert(boot_pgdir_va[0] & PTE_U);  // 验证页目录项的用户权限位
    assert(page_ref(p2) == 1);  // 验证引用计数为 1

    // 将 p1 插入到 PGSIZE 地址，替换 p2
    assert(page_insert(boot_pgdir_va, p1, PGSIZE, 0) == 0);
    assert(page_ref(p1) == 2);  // p1 现在被两个地址引用
    assert(page_ref(p2) == 0);  // p2 应该被释放了
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert((*ptep & PTE_U) == 0);  // 新的权限没有用户位

    // 移除地址 0x0 的映射
    page_remove(boot_pgdir_va, 0x0);
    assert(page_ref(p1) == 1);  // 引用计数减 1
    assert(page_ref(p2) == 0);

    // 移除地址 PGSIZE 的映射
    page_remove(boot_pgdir_va, PGSIZE);
    assert(page_ref(p1) == 0);  // p1 应该被释放了
    assert(page_ref(p2) == 0);

    assert(page_ref(pde2page(boot_pgdir_va[0])) == 1);  // 页目录页的引用计数

    // 清理测试使用的页表结构
    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();  // 刷新 TLB

    assert(nr_free_store == nr_free_pages());  // 验证没有内存泄漏

    cprintf("check_pgdir() succeeded!\n");
}

/**
 * check_boot_pgdir - 检查启动页目录
 * 
 * 该函数测试启动阶段建立的页目录，包括：
 * - 验证内核地址空间的映射
 * - 测试多个虚拟地址映射到同一物理页
 * - 测试通过不同虚拟地址访问同一物理内存
 */
static void check_boot_pgdir(void)
{
    size_t nr_free_store;
    pte_t *ptep;
    int i;

    nr_free_store = nr_free_pages();  // 保存当前空闲页面数

    // 验证内核地址空间的映射是否正确
    // 从 KERNBASE 到物理内存末尾的每一页都应该正确映射
    for (i = ROUNDDOWN(KERNBASE, PGSIZE); i < npage * PGSIZE; i += PGSIZE)
    {
        assert((ptep = get_pte(boot_pgdir_va, (uintptr_t)KADDR(i), 0)) != NULL);
        assert(PTE_ADDR(*ptep) == i);  // 验证虚拟地址到物理地址的映射
    }

    assert(boot_pgdir_va[0] == 0);  // 第一个页目录项应该为空（用户空间）

    struct Page *p;
    p = alloc_page();  // 分配一个测试页面
    
    // 将同一物理页面映射到两个不同的虚拟地址
    assert(page_insert(boot_pgdir_va, p, 0x100, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 1);  // 引用计数为 1
    assert(page_insert(boot_pgdir_va, p, 0x100 + PGSIZE, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 2);  // 引用计数增加到 2

    // 测试通过第一个虚拟地址写入数据
    const char *str = "ucore: Hello world!!";
    strcpy((void *)0x100, str);
    // 验证可以通过第二个虚拟地址读取相同的数据
    assert(strcmp((void *)0x100, (void *)(0x100 + PGSIZE)) == 0);

    // 通过物理地址直接修改内存
    *(char *)(page2kva(p) + 0x100) = '\0';
    // 验证修改对两个虚拟地址都可见
    assert(strlen((const char *)0x100) == 0);

    // 清理测试使用的页表结构
    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(p);
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();  // 刷新 TLB

    assert(nr_free_store == nr_free_pages());  // 验证没有内存泄漏

    cprintf("check_boot_pgdir() succeeded!\n");
}

/**
 * perm2str - 将权限位转换为字符串表示
 * @perm: 权限位
 * @return: 权限字符串，格式为 'u,r,w,-'
 * 
 * 使用字符串 'u,r,w,-' 来表示权限
 * u: 用户权限 (PTE_U)
 * r: 读权限（总是有）
 * w: 写权限 (PTE_W)
 */
static const char *perm2str(int perm)
{
    static char str[4];
    str[0] = (perm & PTE_U) ? 'u' : '-';  // 用户权限
    str[1] = 'r';  // 读权限（RISC-V中总是有）
    str[2] = (perm & PTE_W) ? 'w' : '-';  // 写权限
    str[3] = '\0';
    return str;
}

/**
 * get_pgtable_items - 在页目录或页表的 [left, right] 范围内查找连续的线性地址空间
 * @left: 表范围的左边界（未使用）
 * @right: 表范围的右边界（上界）
 * @start: 表范围的起始位置（下界）
 * @table: 表的起始地址
 * @left_store: 指向表的下一个范围的上界的指针
 * @right_store: 指向表的下一个范围的下界的指针
 * @return: 0 表示没有有效的项范围，否则返回具有 perm 权限的有效项范围
 * 
 * 在 [left, right] 范围内的 PDT 或 PT 中，查找连续的线性地址空间
 * (left_store*X_SIZE~right_store*X_SIZE) 用于 PDT 或 PT
 * X_SIZE=PTSIZE，如果是 PDT；X_SIZE=PGSIZE，如果是 PT
 * 
 * 该函数用于遍历页表，查找具有相同权限的连续页表项
 */
static int get_pgtable_items(size_t left, size_t right, size_t start,
                             uintptr_t *table, size_t *left_store,
                             size_t *right_store)
{
    if (start >= right)
    {
        return 0;  // 超出范围
    }
    
    // 跳过无效的表项
    while (start < right && !(table[start] & PTE_V))
    {
        start++;
    }
    
    if (start < right)  // 找到有效表项
    {
        if (left_store != NULL)
        {
            *left_store = start;  // 记录有效范围的起始位置
        }
        int perm = (table[start++] & PTE_USER);  // 获取权限位
        
        // 查找具有相同权限的连续表项
        while (start < right && (table[start] & PTE_USER) == perm)
        {
            start++;
        }
        
        if (right_store != NULL)
        {
            *right_store = start;  // 记录有效范围的结束位置
        }
        return perm;  // 返回该范围的权限
    }
    return 0;  // 没有找到有效表项
}
