#ifndef __KERN_MM_MEMLAYOUT_H__
#define __KERN_MM_MEMLAYOUT_H__


/* ȫ�ֶκ� */
#define SEG_KTEXT   1
#define SEG_KDATA   2
#define SEG_UTEXT   3
#define SEG_UDATA   4
#define SEG_TSS     5

/* ȫ�������� */
#define GD_KTEXT    ((SEG_KTEXT) << 3)      // �ں��ı�
#define GD_KDATA    ((SEG_KDATA) << 3)      // �ں�����
#define GD_UTEXT    ((SEG_UTEXT) << 3)      // �û��ı�
#define GD_UDATA    ((SEG_UDATA) << 3)      // �û�����
#define GD_TSS      ((SEG_TSS) << 3)        // ����״̬��

#define DPL_KERNEL  (0)
#define DPL_USER    (3)

#define KERNEL_CS   ((GD_KTEXT) | DPL_KERNEL)
#define KERNEL_DS   ((GD_KDATA) | DPL_KERNEL)
#define USER_CS     ((GD_UTEXT) | DPL_USER)
#define USER_DS     ((GD_UDATA) | DPL_USER)



#define KERNBASE            0xC0000000
#define KMEMSIZE            0x38000000                  // ��������ڴ�
#define KERNTOP             (KERNBASE + KMEMSIZE)

#define VPT                 0xFAC00000

#define KSTACKPAGE          2                           // �ں�ջ�е�ҳ
#define KSTACKSIZE          (KSTACKPAGE * PGSIZE)       // �ں�ջ�Ĵ�С

#define USERTOP             0xB0000000
#define USTACKTOP           USERTOP
#define USTACKPAGE          256                         // �û�ջ�е�ҳ
#define USTACKSIZE          (USTACKPAGE * PGSIZE)       // �û�ջ��ҳ�Ĵ�С

#define USERBASE            0x00200000
#define UTEXT               0x00800000                  
#define USTAB               USERBASE                    

#define USER_ACCESS(start, end)                     \
(USERBASE <= (start) && (start) < (end) && (end) <= USERTOP)

#define KERN_ACCESS(start, end)                     \
(KERNBASE <= (start) && (start) < (end) && (end) <= KERNTOP)

#ifndef __ASSEMBLER__

#include <defs.h>
#include <atomic.h>
#include <list.h>

typedef uintptr_t pte_t;
typedef uintptr_t pde_t;
typedef pte_t swap_entry_t; 


#define E820MAX             20      // number of entries in E820MAP
#define E820_ARM            1       // address range memory
#define E820_ARR            2       // address range reserved

struct e820map {
	int nr_map;
	struct {
		uint64_t addr;
		uint64_t size;
		uint32_t type;
	} __attribute__((packed)) map[E820MAX];
};


struct Page {
	int ref;                        // page frame's reference counter
	uint32_t flags;                 // array of flags that describe the status of the page frame
	unsigned int property;          // used in buddy system, stores the order (the X in 2^X) of the continuous memory block
	int zone_num;                   // used in buddy system, the No. of zone which the page belongs to
	list_entry_t page_link;         // free list link
	list_entry_t pra_page_link;     // used for pra (page replace algorithm)
	uintptr_t pra_vaddr;            // used for pra (page replace algorithm)
};

/* ����ҳ���״̬ */
#define PG_reserved                 0       // �Ƿ���
#define PG_property                 1       // �Ƿ����

#define SetPageReserved(page)       set_bit(PG_reserved, &((page)->flags))
#define ClearPageReserved(page)     clear_bit(PG_reserved, &((page)->flags))
#define PageReserved(page)          test_bit(PG_reserved, &((page)->flags))
#define SetPageProperty(page)       set_bit(PG_property, &((page)->flags))
#define ClearPageProperty(page)     clear_bit(PG_property, &((page)->flags))
#define PageProperty(page)          test_bit(PG_property, &((page)->flags))

// list entryת��Ϊpage
#define le2page(le, member)                 \
    to_struct((le), struct Page, member)

typedef struct {
	list_entry_t free_list;         // �б�ͷ��
	unsigned int nr_free;           // ����ҳ��ĸ���
} free_area_t;


#endif /* !__ASSEMBLER__ */

#endif /* !__KERN_MM_MEMLAYOUT_H__ */

