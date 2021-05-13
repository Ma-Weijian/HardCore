#include <stdio.h>
#include <ulib.h>

int main(int argc, char **argv)
{
  if (argc != 3)
  {
    cprintf("wrong param!!\n");
    cprintf("EXAMPLES\n");
    cprintf("     nice 3 14\n");
    cprintf("            change process pid 3's priority to 14.\n");
    cprintf("            valid  process priority is from 1 ~ 19\n");
    return 0;
  }

  int pid = str_to_int(argv[1]);
  int prior = str_to_int(argv[2]);

  if (!(1 <= prior && prior <= 19))
  {
    cprintf("alid process priority is from 1 ~ 19!!!\n");
    return 0;
  }

  int ret = nice(pid, prior);

  if (ret == 0)
    cprintf("process prior has been changed!");
  else if (ret == -1)
    cprintf("No process's pid is %d!", pid);
  cprintf("\n");
  return 0;
}