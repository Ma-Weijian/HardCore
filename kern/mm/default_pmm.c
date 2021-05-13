#include <pmm.h>
#include <list.h>
#include <string.h>
#include <default_pmm.h>

free_area_t free_area;

static void default_init(void)
{
    list_init(&free_area.free_list);
    free_area.nr_free = 0;
}

/* 
    原理大体上如下：
    我们同时维护一个Page数组和记录内存碎片的链表。
    page数组里面有若干页面，在memlayout里面有介绍。
    每一个页面的表项里面都有指向实际内存区域的指针。这个指针应当是被初始化过的，但是这里面并没有出现。
    随后另一个双向链表负责指向实际内存区域的指针，每次读取一个就通过le2page把它转换成page看它的页的数目。
    le2page用的是很暴力的数字节偏移量的方法将表项复原为原来的page。
    注意双向链表是一个环形结构，转到头了代表结束。
*/

static void default_init_memmap(struct Page *base, size_t n)   //Here we have only one slice of memory, and the amount of pages is n.
{
    assert(n > 0);
    struct Page *page_ptr = base;
    while (page_ptr != base + n)
    {
        assert(PageReserved(page_ptr));
        page_ptr->flags = page_ptr->property = 0;
        set_page_ref(page_ptr,0);
        page_ptr++;
    }
    base->property = n;
    SetPageProperty(base);
    free_area.nr_free += n;
    list_add_before(&free_area.free_list, &(base->page_link));
}

static struct Page *default_alloc_pages(size_t num)           //the first-fit version
{
    assert(num>0);
    if(num > free_area.nr_free)
        return NULL;
    
    struct Page *page = NULL;
    list_entry_t *list_entry = &free_area.free_list;
    while ((list_entry = list_next(list_entry)) != &free_area.free_list)
    {
        struct Page *ptr = le2page(list_entry, page_link);
        if(ptr->property >= num)
        {
            page = ptr;
            break;
        }   
    }
    if(page != NULL)
    {
        list_del(&(page->page_link));
        if(page->property > num)
        {
            struct Page *p = page + num;
            p->property = page->property-num;
            SetPageProperty(p);
            list_add_after(&(page->page_link), &(p->page_link));
        }
        list_del(&(page->page_link));
        free_area.nr_free -= num;
        ClearPageProperty(page);
    }
    return page;
}

static struct Page *default_alloc_pages_best_fit(size_t num)           //the best-fit version
{
    assert(num>0);
    if(num > free_area.nr_free)
        return NULL;
    
    struct Page *page = NULL;
    list_entry_t *list_entry = &free_area.free_list;
    int legal_page_min_size = 0x7ffffffff;
    while ((list_entry = list_next(list_entry)) != &free_area.free_list)
    {
        struct Page *ptr = le2page(list_entry, page_link);
        if(ptr->property >= num && ptr->property < legal_page_min_size)
        {
            page = ptr;
            legal_page_min_size = ptr->property;
        }   
    }
    if(page != NULL)
    {
        list_del(&(page->page_link));
        if(page->property > num)
        {
            struct Page *p = page + num;
            p->property = page->property-num;
            SetPageProperty(p);
            list_add_after(&(page->page_link), &(p->page_link));
        }
        list_del(&(page->page_link));
        free_area.nr_free -= num;
        ClearPageProperty(page);
    }
    return page;
}

static struct Page *default_alloc_pages_worst_fit(size_t num)           //the worst-fit version
{
    assert(num>0);
    if(num > free_area.nr_free)
        return NULL;
    
    struct Page *page = NULL;
    list_entry_t *list_entry = &free_area.free_list;
    int max_page_size = 0;
    while ((list_entry = list_next(list_entry)) != &free_area.free_list)
    {
        struct Page *ptr = le2page(list_entry, page_link);
        if(ptr->property >= num && ptr->property >= max_page_size)
        {
            page = ptr;
            max_page_size = ptr->property;
        }   
    }
    if(page != NULL)
    {
        list_del(&(page->page_link));
        if(page->property > num)
        {
            struct Page *p = page + num;
            p->property = page->property-num;
            SetPageProperty(p);
            list_add_after(&(page->page_link), &(p->page_link));
        }
        list_del(&(page->page_link));
        free_area.nr_free -= num;
        ClearPageProperty(page);
    }
    return page;
}


static void default_free_pages(struct Page *base, size_t num)
{
    assert(num > 0);
    struct Page *ptr = base;
    while (ptr != base + num)                     //make sure the pages are not reserved and can be free                         
    {
        assert(!PageReserved(ptr) && !PageProperty(ptr)); 
        ptr->flags = 0;
        set_page_ref(ptr,0);
        ptr++;
    }
    base->property = num;
    SetPageProperty(base);
    list_entry_t *list_entry = &free_area.free_list;
    while ((list_entry = list_next(list_entry)) != &free_area.free_list)
    {
        ptr = le2page(list_entry, page_link);
        if(base+base->property == ptr)
        {
            base->property += ptr->property;
            ClearPageProperty(ptr);
            list_del(&(ptr->page_link));
        }  
        else if(ptr+ptr->property == base)
        {
            ptr->property += base->property;
            ClearPageProperty(base);
            base = ptr;
            list_del(&(ptr->page_link));
        } 
    }  
    free_area.nr_free += num;
    list_entry = &free_area.free_list;
    while ((list_entry = list_next(list_entry)) != &free_area.free_list)
    {
        ptr = le2page(list_entry, page_link);
        if(base + base->property <= ptr)
        {
            assert(base + base->property != ptr);
            break;
        } 
    }   
    list_add_before(list_entry, &(base->page_link));
}

static size_t default_nr_free_pages(void) {
    return free_area.nr_free;
}


static void basic_check(void) {
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    list_entry_t free_list_store = free_area.free_list;
    list_init(&free_area.free_list);
    assert(list_empty(&free_area.free_list));

    unsigned int nr_free_store = free_area.nr_free;
    free_area.nr_free = 0;

    assert(alloc_page() == NULL);

    free_page(p0);
    free_page(p1);
    free_page(p2);
    assert(free_area.nr_free == 3);

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(alloc_page() == NULL);

    free_page(p0);
    assert(!list_empty(&free_area.free_list));

    struct Page *p;
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL);

    assert(free_area.nr_free == 0);
    free_area.free_list = free_list_store;
    free_area.nr_free = nr_free_store;

    free_page(p);
    free_page(p1);
    free_page(p2);
}

static void
default_check(void) {
    int count = 0, total = 0;
    list_entry_t *le = &free_area.free_list;
    while ((le = list_next(le)) != &free_area.free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count ++, total += p->property;
    }
    assert(total == nr_free_pages());

    basic_check();

    struct Page *p0 = alloc_pages(5), *p1, *p2;
    assert(p0 != NULL);
    assert(!PageProperty(p0));

    list_entry_t free_list_store = free_area.free_list;
    list_init(&free_area.free_list);
    assert(list_empty(&free_area.free_list));
    assert(alloc_page() == NULL);

    unsigned int nr_free_store = free_area.nr_free;
    free_area.nr_free = 0;

    free_pages(p0 + 2, 3);
    assert(alloc_pages(4) == NULL);
    assert(PageProperty(p0 + 2) && p0[2].property == 3);
    assert((p1 = alloc_pages(3)) != NULL);
    assert(alloc_page() == NULL);
    assert(p0 + 2 == p1);

    p2 = p0 + 1;
    free_page(p0);
    free_pages(p1, 3);
    assert(PageProperty(p0) && p0->property == 1);
    assert(PageProperty(p1) && p1->property == 3);

    assert((p0 = alloc_page()) == p2 - 1);
    free_page(p0);
    assert((p0 = alloc_pages(2)) == p2 + 1);

    free_pages(p0, 2);
    free_page(p2);

    assert((p0 = alloc_pages(5)) != NULL);
    assert(alloc_page() == NULL);

    assert(free_area.nr_free == 0);
    free_area.nr_free = nr_free_store;

    free_area.free_list = free_list_store;
    free_pages(p0, 5);

    le = &free_area.free_list;
    while ((le = list_next(le)) != &free_area.free_list) {
        assert(le->next->prev == le && le->prev->next == le);
        struct Page *p = le2page(le, page_link);
        count --, total -= p->property;
    }
    assert(count == 0);
    assert(total == 0);
}

static void default_worst_fit_checker(void)
{
    int count = 0, total = 0;
    list_entry_t *le = &free_area.free_list;
    while ((le = list_next(le)) != &free_area.free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count ++, total += p->property;
    }
    assert(total == nr_free_pages());

    basic_check();

    struct Page *p0 = alloc_pages(5), *p1, *p2;
    assert(p0 != NULL);
    assert(!PageProperty(p0));

    list_entry_t free_list_store = free_area.free_list;
    list_init(&free_area.free_list);
    assert(list_empty(&free_area.free_list));
    assert(alloc_page() == NULL);

    cprintf("888888\n");
    unsigned int nr_free_store = free_area.nr_free;
    free_area.nr_free = 0;

    free_pages(p0 + 2, 3);
    assert(alloc_pages(4) == NULL);
    assert(PageProperty(p0 + 2) && p0[2].property == 3);
    assert((p1 = alloc_pages(3)) != NULL);
    assert(alloc_page() == NULL);
    assert(p0 + 2 == p1);

    p2 = p0 + 1;
    free_page(p0);
    free_pages(p1, 3);
    assert(PageProperty(p0) && p0->property == 1);
    assert(PageProperty(p1) && p1->property == 3);


    assert((p0 = alloc_page()) == p2 + 1);
    free_page(p0);
    assert((p0 = alloc_pages(2)) == p2 + 1);

    free_pages(p0, 2);
    free_pages(p2, 1);

    assert((p0 = alloc_pages(5)) != NULL);
    assert(alloc_page() == NULL);

    assert(free_area.nr_free == 0);
    free_area.nr_free = nr_free_store;

    free_area.free_list = free_list_store;
    free_pages(p0, 5);

    le = &free_area.free_list;
    while ((le = list_next(le)) != &free_area.free_list) {
        assert(le->next->prev == le && le->prev->next == le);
        struct Page *p = le2page(le, page_link);
        count --, total -= p->property;
    }
    assert(count == 0);
    assert(total == 0);
}

const struct pmm_manager default_pmm_manager = {
    .name = "default_pmm_manager",
    .init = default_init,
    .init_memmap = default_init_memmap,
	.alloc_pages = default_alloc_pages_best_fit,
    .free_pages = default_free_pages,
    .nr_free_pages = default_nr_free_pages,
	.check = default_check,
};
