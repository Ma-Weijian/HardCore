#include <stdio.h>
#include <syscall.h>
int main()
{
	sys_check_alloc_page();
	print_pgdir();
	return 0;
}