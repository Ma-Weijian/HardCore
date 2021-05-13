#include <defs.h>
#include <x86.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <swap_fifo.h>
#include <list.h>

#include <pmm.h>

list_entry_t pra_list_head;

static int
_fifo_init_mm(struct mm_struct *mm)
{
	list_init(&pra_list_head);
	mm->sm_priv = &pra_list_head;
	//cprintf(" mm->sm_priv %x in fifo_init_mm\n",mm->sm_priv);
	return 0;
}

static int
_fifo_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
	list_entry_t *head = (list_entry_t*)mm->sm_priv;
	list_entry_t *entry = &(page->pra_page_link);

	assert(entry != NULL && head != NULL);
	list_add(head, entry);
	return 0;
}

//先进先出算法
static int
_fifo_swap_out_victim(struct mm_struct *mm, struct Page ** ptr_page, int in_tick)
{
	list_entry_t *head = (list_entry_t*)mm->sm_priv;
	assert(head != NULL);
	assert(in_tick == 0);
	list_entry_t *le = head->prev;
	assert(head != le);

	struct Page *p = le2page(le, pra_page_link);
	list_del(le);
	assert(p != NULL);
	*ptr_page = p;
	return 0;
}

//时钟算法
static int
_enhanced_clock(struct mm_struct *mm, struct Page ** ptr_page, int in_tick)
{
	list_entry_t *head = (list_entry_t*)mm->sm_priv;
	assert(head != NULL);
	assert(in_tick == 0);
	list_entry_t *le = head->prev;
	assert(head != le);

	//找最先进入且未被修改的页面
	while (le != head)
	{
		struct Page *p = le2page(le, pra_page_link);
		pte_t *ptep = get_pte(mm->pgdir, p->pra_vaddr, 0);
		//如果没有被访问过且没有被修改
		if (!(*ptep & PTE_A) && !(*ptep & PTE_D))
		{
			list_del(le);
			assert(p != NULL);
			*ptr_page = p;
			return 0;
		}
		le = le->next;
	}
	le = le->next;
	while (le != head)
	{
		struct Page *p = le2page(le, pra_page_link);
		pte_t *ptep = get_pte(mm->pgdir, p->pra_vaddr, 0);
		//如果没有被访问但是已经被被修改
		if (!(*ptep & PTE_A) && (*ptep & PTE_D))
		{
			list_del(le);
			assert(p != NULL);
			*ptr_page = p;
			return 0;
		}
		*ptep ^= PTE_A;
		le = le->next;
	}
	le = le->next;
	while (le != head)
	{
		struct Page *p = le2page(le, pra_page_link);
		pte_t *ptep = get_pte(mm->pgdir, p->pra_vaddr, 0);
		//如果没有被修改，但是所有页都被访问过
		if (!(*ptep & PTE_D))
		{
			list_del(le);
			assert(p != NULL);
			*ptr_page = p;
			return 0;
		}
		le = le->next;
	}
	le = le->next;
	while (le != head)
	{
		struct Page *p = le2page(le, pra_page_link);
		pte_t *ptep = get_pte(mm->pgdir, p->pra_vaddr, 0);
		//被修改过
		if ((*ptep & PTE_D))
		{
			list_del(le);
			assert(p != NULL);
			*ptr_page = p;
			return 0;
		}
		le = le->next;
	}
}

//改进的时钟算法
static int
_extended_clock(struct mm_struct*mm, struct Page **ptr_page, int in_tick)
{
	list_entry_t*head = (list_entry_t*)mm->sm_priv;
	assert(head != NULL);
	assert(in_tick == 0);

	list_entry_t*le = head->prev;
	assert(head != le);
	while (le != head)
	{
		struct Page*p = le2page(le, pra_page_link);
		//获取页表项
		pte_t*ptep = get_pte(mm->pgdir, p->pra_vaddr, 0);
		//判断获得的页表项是否正确
		if (*ptep < 0x1000)
		{
			break;
		}
		if (!(*ptep&PTE_D))
		{
			//如果dirty bit 为0，换出页面，将页面从队列中删除
			list_del(le);
			assert(p != NULL);
			//将这一个页放在ptr_page中
			*ptr_page = p;
			return 0;
		}
		else {
			//如果为1 ，赋值为0跳过
			*ptep &= 0xffffffbf;
		}
		le = le->next;
	}
	le = head->prev;
	assert(head != le);
	struct Page*p = le2page(le, pra_page_link);

	list_del(le);
	assert(p != NULL);

	*ptr_page = p;
	return 0;
}


int
_fifo_check_swap(void) {
	cprintf("write Virt Page c in fifo_check_swap\n");
	*(unsigned char *)0x3000 = 0x0c;
	assert(pgfault_num == 4);
	cprintf("write Virt Page a in fifo_check_swap\n");
	*(unsigned char *)0x1000 = 0x0a;
	assert(pgfault_num == 4);
	cprintf("write Virt Page d in fifo_check_swap\n");
	*(unsigned char *)0x4000 = 0x0d;
	assert(pgfault_num == 4);
	cprintf("write Virt Page b in fifo_check_swap\n");
	*(unsigned char *)0x2000 = 0x0b;
	assert(pgfault_num == 4);
	cprintf("write Virt Page e in fifo_check_swap\n");
	*(unsigned char *)0x5000 = 0x0e;
	assert(pgfault_num == 5);
	cprintf("write Virt Page b in fifo_check_swap\n");
	*(unsigned char *)0x2000 = 0x0b;
	assert(pgfault_num == 5);
	cprintf("write Virt Page a in fifo_check_swap\n");
	*(unsigned char *)0x1000 = 0x0a;
	assert(pgfault_num == 6);
	cprintf("write Virt Page b in fifo_check_swap\n");
	*(unsigned char *)0x2000 = 0x0b;
	assert(pgfault_num == 7);
	cprintf("write Virt Page c in fifo_check_swap\n");
	*(unsigned char *)0x3000 = 0x0c;
	assert(pgfault_num == 8);
	cprintf("write Virt Page d in fifo_check_swap\n");
	*(unsigned char *)0x4000 = 0x0d;
	assert(pgfault_num == 9);
	cprintf("write Virt Page e in fifo_check_swap\n");
	*(unsigned char *)0x5000 = 0x0e;
	assert(pgfault_num == 10);
	cprintf("write Virt Page a in fifo_check_swap\n");
	assert(*(unsigned char *)0x1000 == 0x0a);
	*(unsigned char *)0x1000 = 0x0a;
	assert(pgfault_num == 11);
	return 0;
}

int
_enhanced_clock_check_swap(void) {
	cprintf("write Virt Page c in enhanced_clock_check_swap\n");
	*(unsigned char *)0x3000 = 0x0c;
	assert(pgfault_num == 4);
	cprintf("write Virt Page a in enhanced_clock_check_swap\n");
	*(unsigned char *)0x1000 = 0x0a;
	assert(pgfault_num == 4);
	cprintf("write Virt Page d in enhanced_clock_check_swap\n");
	*(unsigned char *)0x4000 = 0x0d;
	assert(pgfault_num == 4);
	cprintf("write Virt Page b in enhanced_clock_check_swap\n");
	*(unsigned char *)0x2000 = 0x0b;
	assert(pgfault_num == 4);
	cprintf("write Virt Page e in enhanced_clock_check_swap\n");
	*(unsigned char *)0x5000 = 0x0e;
	assert(pgfault_num == 5);
	cprintf("write Virt Page b in enhanced_clock_check_swap\n");
	*(unsigned char *)0x2000 = 0x0b;
	assert(pgfault_num == 5);
	cprintf("write Virt Page a in enhanced_clock_check_swap\n");
	*(unsigned char *)0x1000 = 0x0a;
	cprintf("Pagefault:%d", pgfault_num);
	assert(pgfault_num == 5);
	cprintf("write Virt Page b in enhanced_clock_check_swap\n");
	*(unsigned char *)0x2000 = 0x0b;
	assert(pgfault_num == 5);
	cprintf("write Virt Page c in enhanced_clock_check_swap\n");
	*(unsigned char *)0x3000 = 0x0c;
	assert(pgfault_num == 5);
	cprintf("write Virt Page d in enhanced_clock_check_swap\n");
	*(unsigned char *)0x4000 = 0x0d;
	assert(pgfault_num == 6);
	cprintf("write Virt Page e in enhanced_clock_check_swap\n");
	*(unsigned char *)0x5000 = 0x0e;
	assert(pgfault_num == 7);
	cprintf("write Virt Page a in enhanced_clock_check_swap\n");
	assert(*(unsigned char *)0x1000 == 0x0a);
	*(unsigned char *)0x1000 = 0x0a;
	assert(pgfault_num == 7);
	return 0;
}

int
_extended_clock_check_swap(void) {
	cprintf("write Virt Page c in extended_clock_check_swap\n");
	*(unsigned char *)0x3000 = 0x0c;
	assert(pgfault_num == 4);
	cprintf("write Virt Page a in extended_clock_check_swap\n");
	*(unsigned char *)0x1000 = 0x0a;
	assert(pgfault_num == 4);
	cprintf("write Virt Page d in extended_clock_check_swap\n");
	*(unsigned char *)0x4000 = 0x0d;
	assert(pgfault_num == 4);
	cprintf("write Virt Page b in extended_clock_check_swap\n");
	*(unsigned char *)0x2000 = 0x0b;
	assert(pgfault_num == 4);
	cprintf("write Virt Page e in extended_clock_check_swap\n");
	*(unsigned char *)0x5000 = 0x0e;
	assert(pgfault_num == 5);
	cprintf("write Virt Page b in extended_clock_check_swap\n");
	*(unsigned char *)0x2000 = 0x0b;
	assert(pgfault_num == 5);
	cprintf("write Virt Page a in extended_clock_check_swap\n");
	*(unsigned char *)0x1000 = 0x0a;
	assert(pgfault_num == 6);
	cprintf("write Virt Page b in extended_clock_check_swap\n");
	*(unsigned char *)0x2000 = 0x0b;
	assert(pgfault_num == 7);
	cprintf("write Virt Page c in extended_clock_check_swap\n");
	*(unsigned char *)0x3000 = 0x0c;
	assert(pgfault_num == 8);
	cprintf("write Virt Page d in extended_clock_check_swap\n");
	*(unsigned char *)0x4000 = 0x0d;
	assert(pgfault_num == 9);
	cprintf("write Virt Page e in extended_clock_check_swap\n");
	*(unsigned char *)0x5000 = 0x0e;
	assert(pgfault_num == 10);
	cprintf("write Virt Page a in extended_clock_check_swap\n");
	assert(*(unsigned char *)0x1000 == 0x0a);
	*(unsigned char *)0x1000 = 0x0a;
	assert(pgfault_num == 11);
	return 0;
}

static int
_fifo_init(void)
{
	return 0;
}

static int
_fifo_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
	return 0;
}

static int
_fifo_tick_event(struct mm_struct *mm)
{
	return 0;
}


struct swap_manager swap_manager_fifo =
{
	 .name = "fifo swap manager",
	 .init = &_fifo_init,
	 .init_mm = &_fifo_init_mm,
	 .tick_event = &_fifo_tick_event,
	 .map_swappable = &_fifo_map_swappable,
	 .set_unswappable = &_fifo_set_unswappable,
	 .swap_out_victim = &_fifo_swap_out_victim,
	 .check_swap = &_fifo_check_swap,
};

struct swap_manager swap_manager_clock =
{
	 .name = "clock swap manager",
	 .init = &_fifo_init,
	 .init_mm = &_fifo_init_mm,
	 .tick_event = &_fifo_tick_event,
	 .map_swappable = &_fifo_map_swappable,
	 .set_unswappable = &_fifo_set_unswappable,
	 .swap_out_victim = &_enhanced_clock,
	 .check_swap = &_enhanced_clock_check_swap,
};

struct swap_manager swap_manager_extended_clock =
{
	 .name = "clock swap manager with dirty bit",
	 .init = &_fifo_init,
	 .init_mm = &_fifo_init_mm,
	 .tick_event = &_fifo_tick_event,
	 .map_swappable = &_fifo_map_swappable,
	 .set_unswappable = &_fifo_set_unswappable,
	 .swap_out_victim = &_extended_clock,
	 .check_swap = &_extended_clock_check_swap,
};