#ifndef __KERN_MM_SWAP_FIFO_H__
#define __KERN_MM_SWAP_FIFO_H__
#define PTE_A  0x020
#define PTE_D  0x040
#include <swap.h>
extern struct swap_manager swap_manager_fifo;
extern struct swap_manager swap_manager_clock;
extern struct swap_manager swap_manager_extended_clock;

int _fifo_check_swap(void);
#endif
