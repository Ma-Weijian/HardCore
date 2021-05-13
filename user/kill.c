#include <stdio.h>
#include <ulib.h>

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    cprintf("wrong param!!\n");
    cprintf("EXAMPLES\n");
    cprintf("     kill 18\n");
    cprintf("            Kill processes which pid is 18.\n");
    return 0;
  }
  int ret = kill(str_to_int(argv[1]));
  if (ret != 0)
    cprintf("process not found !");
  cprintf("\n");
  return 0;
}