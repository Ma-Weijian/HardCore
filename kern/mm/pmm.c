#include <defs.h>
#include <x86.h>
#include <stdio.h>
#include <string.h>
#include <mmu.h>
#include <memlayout.h>
#include <pmm.h>
#include <default_pmm.h>
#include <sync.h>
#include <error.h>
#include <swap.h>
#include <vmm.h>
#include <kmalloc.h>
#include <buddy.h>

static struct taskstate ts = { 0 };

struct Page *pages;

size_t npage = 0;
size_t tpage = 0; // 总共可用的空闲页


extern pde_t __boot_pgdir;
pde_t *boot_pgdir = &__boot_pgdir;

uintptr_t boot_cr3;


const struct pmm_manager *pmm_manager;


pte_t * const vpt = (pte_t *)VPT;
pde_t * const vpd = (pde_t *)PGADDR(PDX(VPT), PDX(VPT), 0);


static struct segdesc gdt[] = {
	SEG_NULL,
	[SEG_KTEXT] = SEG(STA_X | STA_R, 0x0, 0xFFFFFFFF, DPL_KERNEL),
	[SEG_KDATA] = SEG(STA_W, 0x0, 0xFFFFFFFF, DPL_KERNEL),
	[SEG_UTEXT] = SEG(STA_X | STA_R, 0x0, 0xFFFFFFFF, DPL_USER),
	[SEG_UDATA] = SEG(STA_W, 0x0, 0xFFFFFFFF, DPL_USER),
	[SEG_TSS] = SEG_NULL,
};

static struct pseudodesc gdt_pd = {
	sizeof(gdt) - 1, (uintptr_t)gdt
};

void check_alloc_page(void);
void check_pgdir(void);
static void check_boot_pgdir(void);


static inline void
lgdt(struct pseudodesc *pd) {
	asm volatile ("lgdt (%0)" :: "r" (pd));
	asm volatile ("movw %%ax, %%gs" :: "a" (USER_DS));
	asm volatile ("movw %%ax, %%fs" :: "a" (USER_DS));
	asm volatile ("movw %%ax, %%es" :: "a" (KERNEL_DS));
	asm volatile ("movw %%ax, %%ds" :: "a" (KERNEL_DS));
	asm volatile ("movw %%ax, %%ss" :: "a" (KERNEL_DS));
	// reload cs
	asm volatile ("ljmp %0, $1f\n 1:\n" :: "i" (KERNEL_CS));
}


void
load_esp0(uintptr_t esp0) {
	ts.ts_esp0 = esp0;
}

/* gdt_init - initialize the default GDT and TSS */
static void
gdt_init(void) {
	// set boot kernel stack and default SS0
	load_esp0((uintptr_t)bootstacktop);
	ts.ts_ss0 = KERNEL_DS;

	// initialize the TSS filed of the gdt
	gdt[SEG_TSS] = SEGTSS(STS_T32A, (uintptr_t)&ts, sizeof(ts), DPL_KERNEL);

	// reload all segment registers
	lgdt(&gdt_pd);

	// load the TSS
	ltr(GD_TSS);
}

//init_pmm_manager - initialize a pmm_manager instance
static void
init_pmm_manager(void) {
	pmm_manager = &default_pmm_manager;
	cprintf("memory management: %s\n", pmm_manager->name);
	pmm_manager->init();
}

//init_memmap - call pmm->init_memmap to build Page struct for free memory  
static void
init_memmap(struct Page *base, size_t n) {
	pmm_manager->init_memmap(base, n);
}

//alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE memory 
struct Page *
	alloc_pages(size_t n) {
	struct Page *page = NULL;
	bool intr_flag;

	while (1)
	{
		local_intr_save(intr_flag);
		{
			page = pmm_manager->alloc_pages(n);
		}
		local_intr_restore(intr_flag);

		if (page != NULL || n > 1 || swap_init_ok == 0) break;

		extern struct mm_struct *check_mm_struct;
		//cprintf("page %x, call swap_out in alloc_pages %d\n",page, n);
		swap_out(check_mm_struct, n, 0);
	}
	//cprintf("n %d,get page %x, No %d in alloc_pages\n",n,page,(page-pages));
	return page;
}

//free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory 
void
free_pages(struct Page *base, size_t n) {
	bool intr_flag;
	local_intr_save(intr_flag);
	{
		pmm_manager->free_pages(base, n);
	}
	local_intr_restore(intr_flag);
}

//nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE) 
//of current free memory
size_t
nr_free_pages(void) {
	size_t ret;
	bool intr_flag;
	local_intr_save(intr_flag);
	{
		ret = pmm_manager->nr_free_pages();
	}
	local_intr_restore(intr_flag);
	return ret;
}

/* pmm_init - initialize the physical memory management */
static void
page_init(void) {
	struct e820map *memmap = (struct e820map *)(0x8000 + KERNBASE);
	uint64_t maxpa = 0;

	cprintf("e820map:\n");
	int i;
	for (i = 0; i < memmap->nr_map; i++) {
		uint64_t begin = memmap->map[i].addr, end = begin + memmap->map[i].size;
		cprintf("  memory: %08llx, [%08llx, %08llx], type = %d.\n",
			memmap->map[i].size, begin, end - 1, memmap->map[i].type);
		if (memmap->map[i].type == E820_ARM) {
			if (maxpa < end && begin < KMEMSIZE) {
				maxpa = end;
			}
		}
	}
	if (maxpa > KMEMSIZE) {
		maxpa = KMEMSIZE;
	}

	extern char end[];

	npage = maxpa / PGSIZE;
	pages = (struct Page *)ROUNDUP((void *)end, PGSIZE);

	for (i = 0; i < npage; i++) {
		SetPageReserved(pages + i);
	}

	uintptr_t freemem = PADDR((uintptr_t)pages + sizeof(struct Page) * npage);

	for (i = 0; i < memmap->nr_map; i++) {
		uint64_t begin = memmap->map[i].addr, end = begin + memmap->map[i].size;
		if (memmap->map[i].type == E820_ARM) {
			if (begin < freemem) {
				begin = freemem;
			}
			if (end > KMEMSIZE) {
				end = KMEMSIZE;
			}
			if (begin < end) {
				begin = ROUNDUP(begin, PGSIZE);
				end = ROUNDDOWN(end, PGSIZE);
				if (begin < end) {
					init_memmap(pa2page(begin), (end - begin) / PGSIZE);
				}
			}
		}
	}
	tpage = pmm_manager->nr_free_pages();
}


static void
boot_map_segment(pde_t *pgdir, uintptr_t la, size_t size, uintptr_t pa, uint32_t perm) {
	assert(PGOFF(la) == PGOFF(pa));
	size_t n = ROUNDUP(size + PGOFF(la), PGSIZE) / PGSIZE;
	la = ROUNDDOWN(la, PGSIZE);
	pa = ROUNDDOWN(pa, PGSIZE);
	for (; n > 0; n--, la += PGSIZE, pa += PGSIZE) {
		pte_t *ptep = get_pte(pgdir, la, 1);
		assert(ptep != NULL);
		*ptep = pa | PTE_P | perm;
	}
}


static void *
boot_alloc_page(void) {
	struct Page *p = alloc_page();
	if (p == NULL) {
		panic("boot_alloc_page failed.\n");
	}
	return page2kva(p);
}


void
pmm_init(void) {

	boot_cr3 = PADDR(boot_pgdir);
	init_pmm_manager();
	page_init();
	check_alloc_page();

	check_pgdir();

	static_assert(KERNBASE % PTSIZE == 0 && KERNTOP % PTSIZE == 0);


	boot_pgdir[PDX(VPT)] = PADDR(boot_pgdir) | PTE_P | PTE_W;

	boot_map_segment(boot_pgdir, KERNBASE, KMEMSIZE, 0, PTE_W);

	gdt_init();

	print_pgdir();

	kmalloc_init();

	cprintf("kmalloc_init succeeded.\n");

}

pte_t *
get_pte(pde_t *pgdir, uintptr_t la, bool create) {
#if 0
	pde_t *pdep = NULL;   // (1) find page directory entry
	if (0) {              // (2) check if entry is not present
						  // (3) check if creating is needed, then alloc page for page table
						  // CAUTION: this page is used for page table, not for common data page
						  // (4) set page reference
		uintptr_t pa = 0; // (5) get linear address of page
						  // (6) clear page content using memset
						  // (7) set page directory entry's permission
	}
	return NULL;          // (8) return page table entry
#endif
	pde_t *pdep = &pgdir[PDX(la)];
	if (!(*pdep & PTE_P)) {
		struct Page *page;
		if (!create || (page = alloc_page()) == NULL) {
			return NULL;
		}
		set_page_ref(page, 1);
		uintptr_t pa = page2pa(page);
		memset(KADDR(pa), 0, PGSIZE);
		*pdep = pa | PTE_U | PTE_W | PTE_P;
	}
	return &((pte_t *)KADDR(PDE_ADDR(*pdep)))[PTX(la)];
}

struct Page *
	get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store) {
	pte_t *ptep = get_pte(pgdir, la, 0);
	if (ptep_store != NULL) {
		*ptep_store = ptep;
	}
	if (ptep != NULL && *ptep & PTE_P) {
		return pte2page(*ptep);
	}
	return NULL;
}

static inline void
page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep) {
#if 0
	if (0) {                      //(1) check if page directory is present
		struct Page *page = NULL; //(2) find corresponding page to pte
								  //(3) decrease page reference
								  //(4) and free this page when page reference reachs 0
								  //(5) clear second page table entry
								  //(6) flush tlb
	}
#endif
	if (*ptep & PTE_P) {
		struct Page *page = pte2page(*ptep);
		if (page_ref_dec(page) == 0) {
			free_page(page);
		}
		*ptep = 0;
		tlb_invalidate(pgdir, la);
	}
}

void
unmap_range(pde_t *pgdir, uintptr_t start, uintptr_t end) {
	assert(start % PGSIZE == 0 && end % PGSIZE == 0);
	assert(USER_ACCESS(start, end));

	do {
		pte_t *ptep = get_pte(pgdir, start, 0);
		if (ptep == NULL) {
			start = ROUNDDOWN(start + PTSIZE, PTSIZE);
			continue;
		}
		if (*ptep != 0) {
			page_remove_pte(pgdir, start, ptep);
		}
		start += PGSIZE;
	} while (start != 0 && start < end);
}

void
exit_range(pde_t *pgdir, uintptr_t start, uintptr_t end) {
	assert(start % PGSIZE == 0 && end % PGSIZE == 0);
	assert(USER_ACCESS(start, end));

	start = ROUNDDOWN(start, PTSIZE);
	do {
		int pde_idx = PDX(start);
		if (pgdir[pde_idx] & PTE_P) {
			free_page(pde2page(pgdir[pde_idx]));
			pgdir[pde_idx] = 0;
		}
		start += PTSIZE;
	} while (start != 0 && start < end);
}

int
copy_range(pde_t *to, pde_t *from, uintptr_t start, uintptr_t end, bool share) {
	assert(start % PGSIZE == 0 && end % PGSIZE == 0);
	assert(USER_ACCESS(start, end));
	// copy content by page unit.
	do {
		//call get_pte to find process A's pte according to the addr start
		pte_t *ptep = get_pte(from, start, 0), *nptep;
		if (ptep == NULL) {
			start = ROUNDDOWN(start + PTSIZE, PTSIZE);
			continue;
		}
		//call get_pte to find process B's pte according to the addr start. If pte is NULL, just alloc a PT
		if (*ptep & PTE_P) {
			if ((nptep = get_pte(to, start, 1)) == NULL) {
				return -E_NO_MEM;
			}
			uint32_t perm = (*ptep & PTE_USER);
			//get page from ptep
			struct Page *page = pte2page(*ptep);
			// alloc a page for process B
			struct Page *npage = alloc_page();
			assert(page != NULL);
			assert(npage != NULL);
			int ret = 0;
			void * kva_src = page2kva(page);
			void * kva_dst = page2kva(npage);

			memcpy(kva_dst, kva_src, PGSIZE);

			ret = page_insert(to, npage, start, perm);
			assert(ret == 0);
		}
		start += PGSIZE;
	} while (start != 0 && start < end);
	return 0;
}

//page_remove - free an Page which is related linear address la and has an validated pte
void
page_remove(pde_t *pgdir, uintptr_t la) {
	pte_t *ptep = get_pte(pgdir, la, 0);
	if (ptep != NULL) {
		page_remove_pte(pgdir, la, ptep);
	}
}

int
page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm) {
	pte_t *ptep = get_pte(pgdir, la, 1);
	if (ptep == NULL) {
		return -E_NO_MEM;
	}
	page_ref_inc(page);
	if (*ptep & PTE_P) {
		struct Page *p = pte2page(*ptep);
		if (p == page) {
			page_ref_dec(page);
		}
		else {
			page_remove_pte(pgdir, la, ptep);
		}
	}
	*ptep = page2pa(page) | PTE_P | perm;
	tlb_invalidate(pgdir, la);
	return 0;
}

void
tlb_invalidate(pde_t *pgdir, uintptr_t la) {
	if (rcr3() == PADDR(pgdir)) {
		invlpg((void *)la);
	}
}

struct Page *
	pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm) {
	struct Page *page = alloc_page();
	if (page != NULL) {
		if (page_insert(pgdir, page, la, perm) != 0) {
			free_page(page);
			return NULL;
		}
		if (swap_init_ok) {
			if (check_mm_struct != NULL) {
				swap_map_swappable(check_mm_struct, la, page, 0);
				page->pra_vaddr = la;
				assert(page_ref(page) == 1);
				//cprintf("get No. %d  page: pra_vaddr %x, pra_link.prev %x, pra_link_next %x in pgdir_alloc_page\n", (page-pages), page->pra_vaddr,page->pra_page_link.prev, page->pra_page_link.next);
			}
			else {  //now current is existed, should fix it in the future
				//swap_map_swappable(current->mm, la, page, 0);
				//page->pra_vaddr=la;
				//assert(page_ref(page) == 1);
				//panic("pgdir_alloc_page: no pages. now current is existed, should fix it in the future\n");
			}
		}

	}

	return page;
}

void
check_alloc_page(void) {
	pmm_manager->check();
	cprintf("check_alloc_page() succeeded!\n");
}

void
check_pgdir(void) {
	assert(npage <= KMEMSIZE / PGSIZE);
	assert(boot_pgdir != NULL && (uint32_t)PGOFF(boot_pgdir) == 0);
	assert(get_page(boot_pgdir, 0x0, NULL) == NULL);

	struct Page *p1, *p2;
	p1 = alloc_page();
	assert(page_insert(boot_pgdir, p1, 0x0, 0) == 0);

	pte_t *ptep;
	assert((ptep = get_pte(boot_pgdir, 0x0, 0)) != NULL);
	assert(pte2page(*ptep) == p1);
	assert(page_ref(p1) == 1);

	ptep = &((pte_t *)KADDR(PDE_ADDR(boot_pgdir[0])))[1];
	assert(get_pte(boot_pgdir, PGSIZE, 0) == ptep);

	p2 = alloc_page();
	assert(page_insert(boot_pgdir, p2, PGSIZE, PTE_U | PTE_W) == 0);
	assert((ptep = get_pte(boot_pgdir, PGSIZE, 0)) != NULL);
	assert(*ptep & PTE_U);
	assert(*ptep & PTE_W);
	assert(boot_pgdir[0] & PTE_U);
	assert(page_ref(p2) == 1);

	assert(page_insert(boot_pgdir, p1, PGSIZE, 0) == 0);
	assert(page_ref(p1) == 2);
	assert(page_ref(p2) == 0);
	assert((ptep = get_pte(boot_pgdir, PGSIZE, 0)) != NULL);
	assert(pte2page(*ptep) == p1);
	assert((*ptep & PTE_U) == 0);

	page_remove(boot_pgdir, 0x0);
	assert(page_ref(p1) == 1);
	assert(page_ref(p2) == 0);

	page_remove(boot_pgdir, PGSIZE);
	assert(page_ref(p1) == 0);
	assert(page_ref(p2) == 0);

	assert(page_ref(pde2page(boot_pgdir[0])) == 1);
	free_page(pde2page(boot_pgdir[0]));
	boot_pgdir[0] = 0;

	cprintf("check_pgdir() succeeded!\n");
}

static void
check_boot_pgdir(void) {
	pte_t *ptep;
	int i;
	for (i = 0; i < npage; i += PGSIZE) {
		assert((ptep = get_pte(boot_pgdir, (uintptr_t)KADDR(i), 0)) != NULL);
		assert(PTE_ADDR(*ptep) == i);
	}

	assert(PDE_ADDR(boot_pgdir[PDX(VPT)]) == PADDR(boot_pgdir));

	assert(boot_pgdir[0] == 0);

	struct Page *p;
	p = alloc_page();
	assert(page_insert(boot_pgdir, p, 0x100, PTE_W) == 0);
	assert(page_ref(p) == 1);
	assert(page_insert(boot_pgdir, p, 0x100 + PGSIZE, PTE_W) == 0);
	assert(page_ref(p) == 2);

	const char *str = "ucore: Hello world!!";
	strcpy((void *)0x100, str);
	assert(strcmp((void *)0x100, (void *)(0x100 + PGSIZE)) == 0);

	*(char *)(page2kva(p) + 0x100) = '\0';
	assert(strlen((const char *)0x100) == 0);

	free_page(p);
	free_page(pde2page(boot_pgdir[0]));
	boot_pgdir[0] = 0;

	tlb_invalidate(boot_pgdir, 0x100);
	tlb_invalidate(boot_pgdir, 0x100 + PGSIZE);

	cprintf("check_boot_pgdir() succeeded!\n");
}

//perm2str - use string 'u,r,w,-' to present the permission
static const char *
perm2str(int perm) {
	static char str[4];
	str[0] = (perm & PTE_U) ? 'u' : '-';
	str[1] = 'r';
	str[2] = (perm & PTE_W) ? 'w' : '-';
	str[3] = '\0';
	return str;
}

static int
get_pgtable_items(size_t left, size_t right, size_t start, uintptr_t *table, size_t *left_store, size_t *right_store) {
	if (start >= right) {
		return 0;
	}
	while (start < right && !(table[start] & PTE_P)) {
		start++;
	}
	if (start < right) {
		if (left_store != NULL) {
			*left_store = start;
		}
		int perm = (table[start++] & PTE_USER);
		while (start < right && (table[start] & PTE_USER) == perm) {
			start++;
		}
		if (right_store != NULL) {
			*right_store = start;
		}
		return perm;
	}
	return 0;
}

void
print_pgdir(void) {
	cprintf("-------------------- BEGIN --------------------\n");
	size_t left, right = 0, perm;
	while ((perm = get_pgtable_items(0, NPDEENTRY, right, vpd, &left, &right)) != 0) {
		cprintf("PDE(%03x) %08x-%08x %08x %s\n", right - left,
			left * PTSIZE, right * PTSIZE, (right - left) * PTSIZE, perm2str(perm));
		size_t l, r = left * NPTEENTRY;
		while ((perm = get_pgtable_items(left * NPTEENTRY, right * NPTEENTRY, r, vpt, &l, &r)) != 0) {
			cprintf("  |-- PTE(%05x) %08x-%08x %08x %s\n", r - l,
				l * PGSIZE, r * PGSIZE, (r - l) * PGSIZE, perm2str(perm));
		}
	}
	cprintf("--------------------- END ---------------------\n");
}
