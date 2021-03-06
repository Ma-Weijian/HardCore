#include <defs.h>
#include <syscall.h>
#include <stdio.h>
#include <ulib.h>
#include <stat.h>
#include <string.h>
#include <lock.h>

static lock_t fork_lock = INIT_LOCK;

void
lock_fork(void) {
    lock(&fork_lock);
}

void
unlock_fork(void) {
    unlock(&fork_lock);
}

void
exit(int error_code) {
    sys_exit(error_code);
    cprintf("BUG: exit failed.\n");
    while (1);
}

int
fork(void) {
    return sys_fork();
}

int
wait(void) {
    return sys_wait(0, NULL);
}

int
waitpid(int pid, int *store) {
    return sys_wait(pid, store);
}

void
yield(void) {
    sys_yield();
}

int
kill(int pid) {
    return sys_kill(pid);
}

int
getpid(void) {
    return sys_getpid();
}

//print_pgdir - print the PDT&PT
void
print_pgdir(void) {
    sys_pgdir();
}

int
sleep(unsigned int time) {
    return sys_sleep(time);
}

unsigned int
gettime_msec(void) {
    return (unsigned int)sys_gettime();
}

int str_to_int(char *str)
{
  int value = 0;
  for (int i = 0; str[i] != '\0'; i++)
    value = value * 10 + str[i] - '0';
  return value;
}

int
__exec(const char *name, const char **argv) {
    int argc = 0;
    while (argv[argc] != NULL) {
        argc ++;
    }
    return sys_exec(name, argc, argv);
}
