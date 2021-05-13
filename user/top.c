#include <proc.h>

struct proc_struct_user pdb[20];

void print_gdb(struct proc_struct_user *pdb)
{
  cprintf("%d\t%s\t\t", pdb->pid, pdb->name);
  switch (pdb->state)
  {
  case PROC_UNINIT:
    cprintf("UNINIT");
    break;
  case PROC_SLEEPING:
    cprintf("SLEEPING");
    break;
  case PROC_RUNNABLE:
    cprintf("RUNNABLE");
    break;
  case PROC_ZOMBIE:
    cprintf("ZOMBIE\t");
    break;
  default:
    break;
  }
  cprintf("\t%d\t\t", pdb->runs);
  cprintf("%d\t\t", pdb->parent);
  switch (pdb->wait_state)
  {
  case WT_INTERRUPTED:
    cprintf("interrupt\t\t");
    break;
  case WT_CHILD:
    cprintf("child\t\t");
    break;
  case WT_KSEM:
    cprintf("semaphore\t\t");
    break;
  case WT_TIMER:
    cprintf("timer\t\t");
    break;
  case WT_KBD:
    cprintf("input\t\t");
    break;
  default:
    cprintf("none\t\t");
    break;
  }
  if (pdb->is_thread)
    cprintf("true\t");
  else
    cprintf("false\t");
  cprintf("\t%u\n", pdb->prior);
}

int main()
{
  // cprintf("address of pdb is %p\n", pdb);
  int proc_num = get_pdb(pdb);
  cprintf("\nthe num of process is %d\n", proc_num);
  // 总内存是在建立页面管理器后立刻调用 pmm_manager->nr_free_pages() 的结果
  // 空闲内存是在中断调用时实时调用 pmm_manager->nr_free_pages() 的结果
  // 返回的都是页面数，一页有 4kb *4/102 转化成MB即可
  int total_memory = (int)((pdb[0].total_page * 4) / 1024);
  int free_memory = (int)((pdb[0].free_page * 4) / 1024);
  cprintf("total memory: %d MB\tused memory: %d MB\tfree memory: %d MB\n", total_memory, total_memory - free_memory, free_memory);
  cprintf("pid\tname\t\tstate\t\trun time\tparent\t\twait on\t\tis thread?\tnice\n");
  for (int i = 0; i < proc_num; i++)
    print_gdb(&pdb[i]);
  return 0;
}
